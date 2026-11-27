#define _GNU_SOURCE
#include "zvp_capture_probe.h"

#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

extern void zvp_logf(const char *fmt, ...);
extern bool zvp_runtime_is_bypassed(void);

/* ============================================================================
 * 静态拍照管线全链路只读测绘探针（纯内存无锁环形缓冲 + UI 主线程异步落盘）
 * 严禁在 Hook 快路径中调用 open/write/malloc/dlsym(除首次初始化)/dl_iterate_phdr
 * ========================================================================== */

typedef enum {
    ZVP_PROBE_SET_IMG_INFO  = 1,
    ZVP_PROBE_SET_DATA_ALL  = 2,
    ZVP_PROBE_SET_REC_X     = 3,
    ZVP_PROBE_CHK_CANVAS    = 4,
    ZVP_PROBE_GET_CANVAS    = 5,
    ZVP_PROBE_ENCODE_STILL  = 6,
    ZVP_PROBE_CHK_STILL_SZ  = 7,
    ZVP_PROBE_WRITE_STILL   = 8,
    ZVP_PROBE_MAKE_STILL    = 9,
    ZVP_PROBE_GET_REC_INFO  = 10,
    ZVP_PROBE_SET_TGT_SZ    = 11,
    ZVP_PROBE_SET_STILL_SZ  = 12,
    ZVP_PROBE_SET_REC_ASP   = 13,
    ZVP_PROBE_OSAL_MSG      = 14,
    ZVP_PROBE_GET_IMG_INFO  = 15,
    ZVP_PROBE_GET_DATA_ALL  = 16,
    ZVP_PROBE_DFLOW_SEND    = 17,
    ZVP_PROBE_PIN_SEND      = 18,
    ZVP_PROBE_GET_DATA_INFO = 19,
    ZVP_PROBE_SET_EXIF      = 20,
    ZVP_PROBE_ENCODE_STILL4 = 21,
    ZVP_PROBE_MARGIN_SYNC   = 22,
    ZVP_PROBE_STILL_BUFS    = 23,
    ZVP_PROBE_STILL_META    = 24,
    ZVP_PROBE_FLUSH_WRITE   = 25,
    ZVP_PROBE_FILE_OPEN     = 26,
    ZVP_PROBE_FILE_CLOSE    = 27,
    ZVP_PROBE_PLAY_IMAGE    = 28
} zvp_probe_kind_t;

typedef struct zvp_probe_rec {
    uint8_t   kind;
    uint8_t   aspect;
    uint8_t   attach;
    uint8_t   content;
    uintptr_t lr;
    uint32_t  u0;
    uint32_t  u1;
    uint32_t  u2;
    uint16_t  rec_w;
    uint16_t  rec_h;
    uint16_t  can_w;
    uint16_t  can_h;
    uint16_t  voff_x;
    uint16_t  voff_y;
    uint16_t  val_w;
    uint16_t  val_h;
    uint16_t  tgt_w;
    uint16_t  tgt_h;
    uint16_t  unit_hdl;
    uint8_t   data_site;
    uint8_t   compress;
    int32_t   rc;
    uint8_t   raw[48];
    uint8_t   extra[24];
    char      tag[8];
} zvp_probe_rec_t;

#define ZVP_PROBE_RING_CAP   1024u
#define ZVP_PROBE_HASH_CAP   8192u
#define ZVP_VALLOC_MAP_CAP   2048u

typedef struct zvp_probe_dedup_key {
    uint8_t   kind;
    uint8_t   aspect;
    uint8_t   attach;
    uint8_t   content;
    uintptr_t lr;
    uint32_t  u0;
    uint32_t  u1;
    uint32_t  u2;
    uint16_t  rec_w;
    uint16_t  rec_h;
    uint16_t  can_w;
    uint16_t  can_h;
    uint16_t  voff_x;
    uint16_t  voff_y;
    uint16_t  val_w;
    uint16_t  val_h;
    uint16_t  tgt_w;
    uint16_t  tgt_h;
    int32_t   rc;
    uint8_t   raw[48];
} zvp_probe_dedup_key_t;

static zvp_probe_rec_t       s_ring[ZVP_PROBE_RING_CAP];
static volatile uint32_t     s_ring_head = 0u;
static volatile uint32_t     s_ring_tail = 0u;
static volatile int          s_ring_lock = 0;

static volatile uint64_t     s_hash[ZVP_PROBE_HASH_CAP];
static volatile uint64_t     s_valloc_map[ZVP_VALLOC_MAP_CAP];

static volatile uint32_t     s_stat_push      = 0u;
static volatile uint32_t     s_stat_drained   = 0u;
static volatile uint32_t     s_stat_drop_lock = 0u;
static volatile uint32_t     s_stat_drop_hash = 0u;
static volatile uint32_t     s_stat_drop_full = 0u;

static volatile int          s_osal_c_armed = 0;
typedef int (*fn_osal_c_msg_t)(unsigned int msg_id, void *msg);
typedef int (*fn_osal_valloc_t)(unsigned int msg_id, void **pp_msg, unsigned int size, unsigned int wait);
static fn_osal_c_msg_t       s_real_osal_snd    = NULL;
static fn_osal_c_msg_t       s_real_osal_sync   = NULL;
static fn_osal_c_msg_t       s_real_osal_dir    = NULL;
static fn_osal_valloc_t      s_real_osal_valloc = NULL;

static uint64_t fnv1a_64(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0u; i < len; ++i) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return (h == 0ULL) ? 1ULL : h;
}

static bool kind_no_dedup(uint8_t kind)
{
    return (kind == (uint8_t)ZVP_PROBE_ENCODE_STILL ||
            kind == (uint8_t)ZVP_PROBE_ENCODE_STILL4 ||
            kind == (uint8_t)ZVP_PROBE_WRITE_STILL ||
            kind == (uint8_t)ZVP_PROBE_MAKE_STILL ||
            kind == (uint8_t)ZVP_PROBE_SET_TGT_SZ ||
            kind == (uint8_t)ZVP_PROBE_SET_STILL_SZ ||
            kind == (uint8_t)ZVP_PROBE_SET_REC_ASP ||
            kind == (uint8_t)ZVP_PROBE_SET_EXIF ||
            kind == (uint8_t)ZVP_PROBE_MARGIN_SYNC ||
            kind == (uint8_t)ZVP_PROBE_STILL_BUFS ||
            kind == (uint8_t)ZVP_PROBE_STILL_META ||
            kind == (uint8_t)ZVP_PROBE_FLUSH_WRITE ||
            kind == (uint8_t)ZVP_PROBE_FILE_OPEN ||
            kind == (uint8_t)ZVP_PROBE_FILE_CLOSE ||
            kind == (uint8_t)ZVP_PROBE_PLAY_IMAGE);
}

static void probe_push(const zvp_probe_rec_t *rec)
{
    if (!rec) {
        return;
    }

    if (!kind_no_dedup(rec->kind)) {
        zvp_probe_dedup_key_t key;
        memset(&key, 0, sizeof(key));
        key.kind    = rec->kind;
        key.aspect  = rec->aspect;
        key.attach  = rec->attach;
        key.content = rec->content;
        key.lr      = rec->lr;
        key.u0      = rec->u0;
        key.u1      = rec->u1;
        key.u2      = rec->u2;
        key.rec_w   = rec->rec_w;
        key.rec_h   = rec->rec_h;
        key.can_w   = rec->can_w;
        key.can_h   = rec->can_h;
        key.voff_x  = rec->voff_x;
        key.voff_y  = rec->voff_y;
        key.val_w   = rec->val_w;
        key.val_h   = rec->val_h;
        key.tgt_w   = rec->tgt_w;
        key.tgt_h   = rec->tgt_h;
        key.rc      = rec->rc;
        /* 0x014c0424 (ModelStreamingToInstance) 仅填充前 16B，后 28B 为共享池残留垃圾，仅哈希前 16B */
        size_t hash_raw_len = sizeof(key.raw);
        if (rec->kind == (uint8_t)ZVP_PROBE_OSAL_MSG && (rec->u0 & 0x0FFFFFFFu) == 0x014C0424u) {
            hash_raw_len = 16u;
        }
        memcpy(key.raw, rec->raw, hash_raw_len);

        uint64_t fp = fnv1a_64(&key, sizeof(key));
        uint32_t base = (uint32_t)(fp & (ZVP_PROBE_HASH_CAP - 1u));
        bool inserted = false;
        for (uint32_t probe = 0u; probe < 8u; ++probe) {
            uint32_t idx = (base + probe) & (ZVP_PROBE_HASH_CAP - 1u);
            uint64_t cur = __atomic_load_n(&s_hash[idx], __ATOMIC_ACQUIRE);
            if (cur == fp) {
                return;
            }
            if (cur == 0ULL) {
                if (__sync_bool_compare_and_swap(&s_hash[idx], 0ULL, fp)) {
                    inserted = true;
                    break;
                }
                cur = __atomic_load_n(&s_hash[idx], __ATOMIC_ACQUIRE);
                if (cur == fp) {
                    return;
                }
            }
        }
        if (!inserted) {
            __atomic_fetch_add(&s_stat_drop_hash, 1u, __ATOMIC_RELAXED);
            return;
        }
    }

    /* 严禁无限自旋以防实时线程优先级反转死锁；抢占不到直接放弃本条采样 */
    if (!__sync_bool_compare_and_swap(&s_ring_lock, 0, 1)) {
        __atomic_fetch_add(&s_stat_drop_lock, 1u, __ATOMIC_RELAXED);
        return;
    }
    uint32_t tail = __atomic_load_n(&s_ring_tail, __ATOMIC_RELAXED);
    uint32_t next = (tail + 1u) % ZVP_PROBE_RING_CAP;
    uint32_t head = __atomic_load_n(&s_ring_head, __ATOMIC_ACQUIRE);
    if (next != head) {
        s_ring[tail] = *rec;
        __atomic_store_n(&s_ring_tail, next, __ATOMIC_RELEASE);
        __atomic_fetch_add(&s_stat_push, 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&s_stat_drop_full, 1u, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_ring_lock, 0, __ATOMIC_RELEASE);
}

void zvp_capture_probe_get_stats(uint32_t *out_push, uint32_t *out_drained,
                                 uint32_t *out_drop_lock, uint32_t *out_drop_hash,
                                 uint32_t *out_drop_full, int *out_lock)
{
    if (out_push)      *out_push      = __atomic_load_n(&s_stat_push, __ATOMIC_RELAXED);
    if (out_drained)   *out_drained   = __atomic_load_n(&s_stat_drained, __ATOMIC_RELAXED);
    if (out_drop_lock) *out_drop_lock = __atomic_load_n(&s_stat_drop_lock, __ATOMIC_RELAXED);
    if (out_drop_hash) *out_drop_hash = __atomic_load_n(&s_stat_drop_hash, __ATOMIC_RELAXED);
    if (out_drop_full) *out_drop_full = __atomic_load_n(&s_stat_drop_full, __ATOMIC_RELAXED);
    if (out_lock)      *out_lock      = __atomic_load_n(&s_ring_lock, __ATOMIC_RELAXED);
}

static void fill_img_info_fields(zvp_probe_rec_t *rec, const uint8_t *base)
{
    rec->attach    = base[0x00u];
    memcpy(&rec->u0, base + 0x04u, sizeof(uint32_t));
    memcpy(&rec->unit_hdl, base + 0x08u, sizeof(uint16_t));
    memcpy(&rec->rec_w,  base + 0x14u, sizeof(uint16_t));
    memcpy(&rec->rec_h,  base + 0x16u, sizeof(uint16_t));
    memcpy(&rec->can_w,  base + 0x18u, sizeof(uint16_t));
    memcpy(&rec->can_h,  base + 0x1Au, sizeof(uint16_t));
    memcpy(&rec->voff_x, base + 0x1Cu, sizeof(uint16_t));
    memcpy(&rec->voff_y, base + 0x1Eu, sizeof(uint16_t));
    memcpy(&rec->val_w,  base + 0x20u, sizeof(uint16_t));
    memcpy(&rec->val_h,  base + 0x22u, sizeof(uint16_t));
    memcpy(&rec->tgt_w,  base + 0x28u, sizeof(uint16_t));
    memcpy(&rec->tgt_h,  base + 0x2Au, sizeof(uint16_t));
    rec->data_site = base[0x2Cu];
    rec->aspect    = base[0x2Eu];
    rec->compress  = base[0x2Fu];
    rec->content   = base[0x32u];
}

/* ============================================================================
 * 1. DataflowInfra_StdData_YC::SetEntry_ImageSizeInfo & Entry::SetData_All
 * ========================================================================== */
typedef int  (*fn_set_entry_imgsize_t)(void *self, uint8_t *entry);
typedef void (*fn_set_data_all_t)(void *self, const uint8_t *info);

int _ZN26C_DataflowInfra_StdData_YC22SetEntry_ImageSizeInfoEP35C_DataflowInfra_Entry_ImageSizeInfo(void *self, uint8_t *entry)
{
    static fn_set_entry_imgsize_t real_fn = NULL;
    fn_set_entry_imgsize_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_entry_imgsize_t)dlsym(RTLD_NEXT, "_ZN26C_DataflowInfra_StdData_YC22SetEntry_ImageSizeInfoEP35C_DataflowInfra_Entry_ImageSizeInfo");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed() && entry) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_IMG_INFO;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        fill_img_info_fields(&rec, entry + 0x14u);
        probe_push(&rec);
    }
    return fn ? fn(self, entry) : -1;
}

void _ZN35C_DataflowInfra_Entry_ImageSizeInfo11SetData_AllEPN19DataflowInfra_StdYc22PIN_YC_IMAGE_SIZE_INFOE(void *self, const uint8_t *info)
{
    static fn_set_data_all_t real_fn = NULL;
    fn_set_data_all_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_data_all_t)dlsym(RTLD_NEXT, "_ZN35C_DataflowInfra_Entry_ImageSizeInfo11SetData_AllEPN19DataflowInfra_StdYc22PIN_YC_IMAGE_SIZE_INFOE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed() && info) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_DATA_ALL;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        fill_img_info_fields(&rec, info);
        probe_push(&rec);
    }
    if (fn) fn(self, info);
}

typedef void (*fn_set_u16_t)(void *self, uint16_t val);

void _ZN35C_DataflowInfra_Entry_ImageSizeInfo19SetData_RecordSizeXEt(void *self, uint16_t val)
{
    static fn_set_u16_t real_fn = NULL;
    fn_set_u16_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_u16_t)dlsym(RTLD_NEXT, "_ZN35C_DataflowInfra_Entry_ImageSizeInfo19SetData_RecordSizeXEt");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (fn) fn(self, val);
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind  = (uint8_t)ZVP_PROBE_SET_REC_X;
        rec.lr    = (uintptr_t)__builtin_return_address(0);
        rec.rec_w = val;
        probe_push(&rec);
    }
}

/* ============================================================================
 * 2. CanvasSizeAcsr & UtilStillSize
 * ========================================================================== */
typedef int (*fn_chk_canvas_still_t)(void *self, uint16_t w, uint16_t h);
typedef int (*fn_get_canvas_still_t)(void *self, uint32_t size_idx, int aspect, uint16_t *out_rect);
typedef int (*fn_util_chk_still_t)(void *tbl, int cam_mode, uint32_t rect_wh, uint8_t *out_ok, int *out_st);

int _ZN17OBJAVBBCBASECLASS14CanvasSizeAcsr20CheckCanvasSizeStillEtt(void *self, uint16_t w, uint16_t h)
{
    static fn_chk_canvas_still_t real_fn = NULL;
    fn_chk_canvas_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_chk_canvas_still_t)dlsym(RTLD_NEXT, "_ZN17OBJAVBBCBASECLASS14CanvasSizeAcsr20CheckCanvasSizeStillEtt");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(self, w, h) : 0;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind  = (uint8_t)ZVP_PROBE_CHK_CANVAS;
        rec.lr    = (uintptr_t)__builtin_return_address(0);
        rec.val_w = w;
        rec.val_h = h;
        rec.rc    = rc;
        probe_push(&rec);
    }
    return rc;
}

int _ZN17OBJAVBBCBASECLASS14CanvasSizeAcsr13GetCanvasSizeEjNS0_12STILL_ASPECTEPN8DefPinYc8RectSizeE(void *self, uint32_t size_idx, int aspect, uint16_t *out_rect)
{
    static fn_get_canvas_still_t real_fn = NULL;
    fn_get_canvas_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_get_canvas_still_t)dlsym(RTLD_NEXT, "_ZN17OBJAVBBCBASECLASS14CanvasSizeAcsr13GetCanvasSizeEjNS0_12STILL_ASPECTEPN8DefPinYc8RectSizeE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(self, size_idx, aspect, out_rect) : -1;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind   = (uint8_t)ZVP_PROBE_GET_CANVAS;
        rec.lr     = (uintptr_t)__builtin_return_address(0);
        rec.u0     = size_idx;
        rec.aspect = (uint8_t)(aspect & 0xFF);
        if (out_rect) {
            rec.can_w = out_rect[0];
            rec.can_h = out_rect[1];
        }
        rec.rc = rc;
        probe_push(&rec);
    }
    return rc;
}

int _ZN13UtilStillSize14CheckStillSizeEPN12MprCtrlStill15_StillSizeTableEN9DefStruct10CameraModeENS_8RectSizeEPbPNS_10SizeStatusE(
    void *tbl, int cam_mode, uint32_t rect_wh, uint8_t *out_ok, int *out_st)
{
    static fn_util_chk_still_t real_fn = NULL;
    fn_util_chk_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_util_chk_still_t)dlsym(RTLD_NEXT, "_ZN13UtilStillSize14CheckStillSizeEPN12MprCtrlStill15_StillSizeTableEN9DefStruct10CameraModeENS_8RectSizeEPbPNS_10SizeStatusE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(tbl, cam_mode, rect_wh, out_ok, out_st) : 0;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind   = (uint8_t)ZVP_PROBE_CHK_STILL_SZ;
        rec.lr     = (uintptr_t)__builtin_return_address(0);
        rec.u0     = (uint32_t)cam_mode;
        rec.val_w  = (uint16_t)(rect_wh & 0xFFFFu);
        rec.val_h  = (uint16_t)((rect_wh >> 16) & 0xFFFFu);
        rec.aspect = out_ok ? *out_ok : 0xFFu;
        rec.rc     = out_st ? *out_st : rc;
        probe_push(&rec);
    }
    return rc;
}

/* ============================================================================
 * 3. MprCtrl::GetStillRecInfo & MprIf 静态拍摄/写卡系列
 * ========================================================================== */
typedef int (*fn_get_still_rec_info_t)(uint8_t *out_info, int *p1, int *p2);
typedef unsigned long (*fn_encode_still_t)(void *self, uint32_t *param);
typedef unsigned long (*fn_write_still_t)(void *self, uint8_t *param, uint8_t *info, unsigned long count);
typedef unsigned long (*fn_make_write_still_t)(void *self, uint8_t *param, uint8_t *info);
typedef unsigned long (*fn_set_tgt_still_t)(void *self, unsigned long a1, int rec_type, unsigned long a3);
typedef unsigned long (*fn_set_still_sz_t)(void *self, unsigned long a1, unsigned long *sz_ptr, unsigned long a3);
typedef unsigned long (*fn_set_rec_asp_t)(void *self, uint32_t *asp_prm, unsigned long a2);

int _ZN7MprCtrl15GetStillRecInfoEPN9DefStruct13StillSizeInfoEPiS3_(uint8_t *out_info, int *p1, int *p2)
{
    static fn_get_still_rec_info_t real_fn = NULL;
    fn_get_still_rec_info_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_get_still_rec_info_t)dlsym(RTLD_NEXT, "_ZN7MprCtrl15GetStillRecInfoEPN9DefStruct13StillSizeInfoEPiS3_");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(out_info, p1, p2) : -1;
    if (!zvp_runtime_is_bypassed() && out_info) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_GET_REC_INFO;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        memcpy(rec.raw, out_info, 24u);
        rec.u0   = p1 ? (uint32_t)*p1 : 0u;
        rec.u1   = p2 ? (uint32_t)*p2 : 0u;
        rec.rc   = rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf11EncodeStillEPNS_16EncodeStillParamE(void *self, uint32_t *param)
{
    static fn_encode_still_t real_fn = NULL;
    fn_encode_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_encode_still_t)dlsym(RTLD_NEXT, "_ZN5MprIf11EncodeStillEPNS_16EncodeStillParamE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, param) : 1ul;
    if (!zvp_runtime_is_bypassed() && param) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind  = (uint8_t)ZVP_PROBE_ENCODE_STILL;
        rec.lr    = (uintptr_t)__builtin_return_address(0);
        rec.u0    = param[2];
        rec.rec_w = (uint16_t)(param[3] & 0xFFFFu);
        rec.rec_h = (uint16_t)((param[3] >> 16) & 0xFFFFu);
        rec.can_w = (uint16_t)(param[4] & 0xFFFFu);
        rec.can_h = (uint16_t)((param[4] >> 16) & 0xFFFFu);
        rec.val_w = (uint16_t)(param[5] & 0xFFFFu);
        rec.val_h = (uint16_t)((param[5] >> 16) & 0xFFFFu);
        rec.rc    = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf10WriteStillEPNS_15WriteStillParamEPN3MPR9StillInfoEm(void *self, uint8_t *param, uint8_t *info, unsigned long count)
{
    static fn_write_still_t real_fn = NULL;
    fn_write_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_write_still_t)dlsym(RTLD_NEXT, "_ZN5MprIf10WriteStillEPNS_15WriteStillParamEPN3MPR9StillInfoEm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, param, info, count) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_WRITE_STILL;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)count;
        if (param) {
            memcpy(rec.raw, param, 24u);
            memcpy(rec.extra, param + 48u, 24u);
            /* MprIfLog::WriteStill 读 +6,+7,+8 字节及 +12,+16,+20 (u16), +24 (u32) */
            memcpy(&rec.rec_w, param + 12u, sizeof(uint16_t));
            memcpy(&rec.rec_h, param + 16u, sizeof(uint16_t));
            memcpy(&rec.can_w, param + 20u, sizeof(uint16_t));
            memcpy(&rec.u1,    param + 24u, sizeof(uint32_t));
        }
        if (info) {
            memcpy(&rec.val_w,  info + 0u,  sizeof(uint16_t));
            memcpy(&rec.val_h,  info + 2u,  sizeof(uint16_t));
            memcpy(&rec.tgt_w,  info + 4u,  sizeof(uint16_t));
            memcpy(&rec.tgt_h,  info + 6u,  sizeof(uint16_t));
            memcpy(&rec.voff_x, info + 8u,  sizeof(uint16_t));
            memcpy(&rec.voff_y, info + 10u, sizeof(uint16_t));
            memcpy(&rec.can_h,  info + 12u, sizeof(uint16_t));
            memcpy(&rec.u2,     info + 16u, sizeof(uint32_t));
        }
        rec.rc = (int32_t)rc;
        probe_push(&rec);

        /* 抓取 StillInfo 中的核心 BufferArea {ptr, size} 描述符（不解引用指针，零崩风险） */
        if (info) {
            zvp_probe_rec_t brec;
            memset(&brec, 0, sizeof(brec));
            brec.kind = (uint8_t)ZVP_PROBE_STILL_BUFS;
            brec.lr   = rec.lr;
            memcpy(&brec.u0, info + 0xC8u, sizeof(uint32_t)); /* item count */
            memcpy(brec.raw + 0u,  info + 0x08u, 8u);
            memcpy(brec.raw + 8u,  info + 0x18u, 8u);
            memcpy(brec.raw + 16u, info + 0xD0u, 8u);
            memcpy(brec.raw + 24u, info + 0xE0u, 8u);
            memcpy(brec.raw + 32u, info + 0xF0u, 8u);
            memcpy(brec.raw + 40u, info + 0x100u, 8u);
            memcpy(brec.extra + 0u,  info + 0x110u, 8u);
            memcpy(brec.extra + 8u,  info + 0x120u, 8u);
            memcpy(brec.extra + 16u, info + 0x130u, 8u);
            probe_push(&brec);

            zvp_probe_rec_t mrec;
            memset(&mrec, 0, sizeof(mrec));
            mrec.kind = (uint8_t)ZVP_PROBE_STILL_META;
            mrec.lr   = rec.lr;
            memcpy(&mrec.u0, info + 0xC8u, sizeof(uint32_t));
            memcpy(mrec.raw,   info + 0x20u, 48u);
            memcpy(mrec.extra, info + 0x50u, 24u);
            probe_push(&mrec);
        }
    }
    return rc;
}

typedef unsigned long (*fn_flush_write_still_t)(void *self, uint8_t *param, unsigned long a2);
typedef unsigned long (*fn_play_still_image_t)(void *self, unsigned long media, uint8_t *in_p, uint8_t *out_p);
typedef unsigned long (*fn_mpr_file_open_t)(const char *path, uint32_t mode, void **out_hdl);
typedef unsigned long (*fn_mpr_file_close_t)(void *hdl, bool b);

unsigned long _ZN5MprIf15FlushWriteStillEPNS_20FlushWriteStillParamEm(void *self, uint8_t *param, unsigned long a2)
{
    static fn_flush_write_still_t real_fn = NULL;
    fn_flush_write_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_flush_write_still_t)dlsym(RTLD_NEXT, "_ZN5MprIf15FlushWriteStillEPNS_20FlushWriteStillParamEm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, param, a2) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_FLUSH_WRITE;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)a2;
        if (param) memcpy(rec.raw, param, 48u);
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf14PlayStillImageEmPNS_21PlayStillImageInParamEPNS_22PlayStillImageOutParamE(
    void *self, unsigned long media, uint8_t *in_p, uint8_t *out_p)
{
    static fn_play_still_image_t real_fn = NULL;
    fn_play_still_image_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_play_still_image_t)dlsym(RTLD_NEXT, "_ZN5MprIf14PlayStillImageEmPNS_21PlayStillImageInParamEPNS_22PlayStillImageOutParamE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, media, in_p, out_p) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_PLAY_IMAGE;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)media;
        if (in_p)  memcpy(rec.raw,   in_p,  48u);
        if (out_p) memcpy(rec.extra, out_p, 24u);
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN9MprFileIf4OpenEPKcN3MPR8OpenModeEPPv(const char *path, uint32_t mode, void **out_hdl)
{
    static fn_mpr_file_open_t real_fn = NULL;
    fn_mpr_file_open_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_mpr_file_open_t)dlsym(RTLD_NEXT, "_ZN9MprFileIf4OpenEPKcN3MPR8OpenModeEPPv");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(path, mode, out_hdl) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_FILE_OPEN;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = mode;
        rec.u1   = out_hdl ? (uint32_t)(uintptr_t)(*out_hdl) : 0u;
        if (path) {
            for (size_t i = 0u; i < sizeof(rec.raw) - 1u && path[i]; ++i) {
                rec.raw[i] = (uint8_t)path[i];
            }
        }
        rec.rc = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN9MprFileIf5CloseEPvb(void *hdl, bool b)
{
    static fn_mpr_file_close_t real_fn = NULL;
    fn_mpr_file_close_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_mpr_file_close_t)dlsym(RTLD_NEXT, "_ZN9MprFileIf5CloseEPvb");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(hdl, b) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_FILE_CLOSE;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)(uintptr_t)hdl;
        rec.u1   = b ? 1u : 0u;
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf19MakeWriteStillImageEPNS_20WriteStillImageParamEPN3MPR9StillInfoE(void *self, uint8_t *param, uint8_t *info)
{
    static fn_make_write_still_t real_fn = NULL;
    fn_make_write_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_make_write_still_t)dlsym(RTLD_NEXT, "_ZN5MprIf19MakeWriteStillImageEPNS_20WriteStillImageParamEPN3MPR9StillInfoE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, param, info) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_MAKE_STILL;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        if (param) memcpy(rec.raw, param, 48u);
        if (info) {
            memcpy(rec.extra, info, 24u);
            memcpy(&rec.val_w,  info + 0u,  sizeof(uint16_t));
            memcpy(&rec.val_h,  info + 2u,  sizeof(uint16_t));
            memcpy(&rec.tgt_w,  info + 4u,  sizeof(uint16_t));
            memcpy(&rec.tgt_h,  info + 6u,  sizeof(uint16_t));
            memcpy(&rec.voff_x, info + 8u,  sizeof(uint16_t));
            memcpy(&rec.voff_y, info + 10u, sizeof(uint16_t));
        }
        rec.rc = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf18SetTargetStillSizeEmN3MPR12StillRecTypeEm(void *self, unsigned long a1, int rec_type, unsigned long a3)
{
    static fn_set_tgt_still_t real_fn = NULL;
    fn_set_tgt_still_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_tgt_still_t)dlsym(RTLD_NEXT, "_ZN5MprIf18SetTargetStillSizeEmN3MPR12StillRecTypeEm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, a1, rec_type, a3) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_TGT_SZ;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)a1;
        rec.u1   = (uint32_t)rec_type;
        rec.u2   = (uint32_t)a3;
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf12SetStillSizeEmPmm(void *self, unsigned long a1, unsigned long *sz_ptr, unsigned long a3)
{
    static fn_set_still_sz_t real_fn = NULL;
    fn_set_still_sz_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_still_sz_t)dlsym(RTLD_NEXT, "_ZN5MprIf12SetStillSizeEmPmm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, a1, sz_ptr, a3) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_STILL_SZ;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)a1;
        rec.u1   = sz_ptr ? (uint32_t)sz_ptr[0] : 0u;
        rec.u2   = (uint32_t)a3;
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf12SetRecAspectEPNS_12VAspectParamEm(void *self, uint32_t *asp_prm, unsigned long a2)
{
    static fn_set_rec_asp_t real_fn = NULL;
    fn_set_rec_asp_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_rec_asp_t)dlsym(RTLD_NEXT, "_ZN5MprIf12SetRecAspectEPNS_12VAspectParamEm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, asp_prm, a2) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_REC_ASP;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = asp_prm ? asp_prm[0] : 0u;
        rec.u1   = asp_prm ? asp_prm[1] : 0u;
        rec.u2   = (uint32_t)a2;
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

/* ============================================================================
 * 4. Avbbc OSAL 全消息通道（OsalSndMsg / OsalSndMsgGlobal / OsalSndMsgDataflow）
 * ========================================================================== */
static void probe_on_osal_ex(uintptr_t lr, const char *tag,
                             uint32_t msg_id, const uint8_t *data,
                             uint32_t read_len, uint32_t alloc_len)
{
    if (zvp_runtime_is_bypassed() || !data || read_len < 4u) {
        return;
    }
    uint32_t mid_low = msg_id & 0x0FFFFFFFu;
    uint16_t cmd = (uint16_t)(((uint16_t)data[3] << 8) | data[2]);

    /* 过滤音频/BizLog/本地MWF队列/20ms定时器/陀螺仪与曝光/按键轮询/Shell工具控制噪声 */
    if (mid_low == 0x0061003Cu) return;
    if (mid_low == 0x008F013Au || mid_low == 0x008F0139u) return;
    if (mid_low == 0x0080003Bu) return;
    if (mid_low == 0x00910041u || mid_low == 0x00910040u) return;
    if (mid_low == 0x003E014Du) return;
    if (mid_low == 0x0097E124u || mid_low == 0x0030E075u) return;
    if (mid_low == 0x00720018u) return;
    if (mid_low == 0x016F0480u) return;
    if (mid_low == 0x00924127u && cmd == 0x0092u && data[0] == 0x11u) return;
    if (data[0] == 0x4Du && data[1] == 0x57u && data[2] == 0x46u && data[3] == 0x01u) return;
    if (mid_low == 0x0058003Fu && ((cmd & 0xFF00u) == 0x0200u || cmd == 0x0424u)) return;
    if (mid_low == 0x007AE05Du && cmd == 0x1507u) return;
    if ((mid_low == 0x007A0020u || mid_low == 0x007A804Cu) && cmd == 0x1800u) return;
    if ((mid_low == 0x0002E15Bu || mid_low == 0x00040193u) && cmd == 0x4049u) return;

    zvp_probe_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.kind = (uint8_t)ZVP_PROBE_OSAL_MSG;
    rec.lr   = lr;
    rec.u0   = msg_id;
    rec.u1   = ((uint32_t)cmd << 16) | (read_len & 0xFFFFu);
    rec.u2   = alloc_len;
    if (tag) {
        for (size_t i = 0u; i < sizeof(rec.tag) - 1u && tag[i]; ++i) {
            rec.tag[i] = tag[i];
        }
    }
    size_t cpy = (read_len < sizeof(rec.raw)) ? (size_t)read_len : sizeof(rec.raw);
    memcpy(rec.raw, data, cpy);
    probe_push(&rec);
}

void zvp_capture_probe_on_osal(uintptr_t lr, const char *tag,
                               uint32_t msg_id, const uint8_t *data, uint32_t len)
{
    probe_on_osal_ex(lr, tag, msg_id, data, len, len);
}

typedef int (*fn_osalsnd_ex_t)(void *self, unsigned int msg_id, const unsigned char *data, unsigned int len);

int _ZN11ObjAvbbcCmn16OsalSndMsgGlobalEjPKhj(void *self, unsigned int msg_id, const unsigned char *data, unsigned int len)
{
    static fn_osalsnd_ex_t real_fn = NULL;
    fn_osalsnd_ex_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osalsnd_ex_t)dlsym(RTLD_NEXT, "_ZN11ObjAvbbcCmn16OsalSndMsgGlobalEjPKhj");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed()) {
        zvp_capture_probe_on_osal((uintptr_t)__builtin_return_address(0), "Global", msg_id, data, len);
    }
    return fn ? fn(self, msg_id, data, len) : -1;
}

int _ZN11ObjAvbbcCmn18OsalSndMsgDataflowEjPKhj(void *self, unsigned int msg_id, const unsigned char *data, unsigned int len)
{
    static fn_osalsnd_ex_t real_fn = NULL;
    fn_osalsnd_ex_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osalsnd_ex_t)dlsym(RTLD_NEXT, "_ZN11ObjAvbbcCmn18OsalSndMsgDataflowEjPKhj");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed()) {
        zvp_capture_probe_on_osal((uintptr_t)__builtin_return_address(0), "Dflow", msg_id, data, len);
    }
    return fn ? fn(self, msg_id, data, len) : -1;
}

int _ZN11ObjAvbbcCmn14OsalSndSyncMsgEjPKhj(void *self, unsigned int msg_id, const unsigned char *data, unsigned int len)
{
    static fn_osalsnd_ex_t real_fn = NULL;
    fn_osalsnd_ex_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osalsnd_ex_t)dlsym(RTLD_NEXT, "_ZN11ObjAvbbcCmn14OsalSndSyncMsgEjPKhj");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed()) {
        zvp_capture_probe_on_osal((uintptr_t)__builtin_return_address(0), "Sync", msg_id, data, len);
    }
    return fn ? fn(self, msg_id, data, len) : -1;
}

/* ============================================================================
 * 5. GetEntry_ImageSizeInfo / GetData_All / DataflowInfraSender / Pin / Exif
 * ========================================================================== */
typedef int  (*fn_get_entry_imgsize_t)(void *self, uint8_t *entry);
typedef void (*fn_get_data_all_t)(void *self, uint8_t *info);
typedef int  (*fn_dflow_send_async_t)(void *self, unsigned int msg_id, const uint8_t *msg);
typedef int  (*fn_dflow_pin_send_t)(void *self, unsigned long a1, unsigned short a2, unsigned long a3);
typedef int  (*fn_get_still_data_info_t)(int a0, int a1, int a2, int a3, int a4, int a5, uint8_t *out_info);
typedef unsigned long (*fn_set_exif_info_t)(void *self, int flag, const uint32_t *buf_area);
typedef unsigned long (*fn_encode_still4_t)(void *self, const uint8_t *region, const uint8_t *ltm, const uint8_t *meta, const uint8_t *jpg);
typedef unsigned long (*fn_set_margin_sync_t)(void *self, unsigned long a1, unsigned long a2, unsigned long a3);

int _ZN26C_DataflowInfra_StdData_YC22GetEntry_ImageSizeInfoEP35C_DataflowInfra_Entry_ImageSizeInfo(void *self, uint8_t *entry)
{
    static fn_get_entry_imgsize_t real_fn = NULL;
    fn_get_entry_imgsize_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_get_entry_imgsize_t)dlsym(RTLD_NEXT, "_ZN26C_DataflowInfra_StdData_YC22GetEntry_ImageSizeInfoEP35C_DataflowInfra_Entry_ImageSizeInfo");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(self, entry) : -1;
    if (!zvp_runtime_is_bypassed() && entry && rc >= 0) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_GET_IMG_INFO;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        fill_img_info_fields(&rec, entry + 0x14u);
        rec.rc   = rc;
        if (rec.rec_w > 0u || rec.can_w > 0u || rec.val_w > 0u || rec.tgt_w > 0u) {
            probe_push(&rec);
        }
    }
    return rc;
}

void _ZN35C_DataflowInfra_Entry_ImageSizeInfo11GetData_AllEPN19DataflowInfra_StdYc22PIN_YC_IMAGE_SIZE_INFOE(void *self, uint8_t *info)
{
    static fn_get_data_all_t real_fn = NULL;
    fn_get_data_all_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_get_data_all_t)dlsym(RTLD_NEXT, "_ZN35C_DataflowInfra_Entry_ImageSizeInfo11GetData_AllEPN19DataflowInfra_StdYc22PIN_YC_IMAGE_SIZE_INFOE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (fn) fn(self, info);
    if (!zvp_runtime_is_bypassed() && info) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_GET_DATA_ALL;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        fill_img_info_fields(&rec, info);
        if (rec.rec_w > 0u || rec.can_w > 0u || rec.val_w > 0u || rec.tgt_w > 0u) {
            probe_push(&rec);
        }
    }
}

int _ZN19DataflowInfraSender9SendASyncEjP16DataflowInfraMsg(void *self, unsigned int msg_id, const uint8_t *msg)
{
    static fn_dflow_send_async_t real_fn = NULL;
    fn_dflow_send_async_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_dflow_send_async_t)dlsym(RTLD_NEXT, "_ZN19DataflowInfraSender9SendASyncEjP16DataflowInfraMsg");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    if (!zvp_runtime_is_bypassed() && msg) {
        uint32_t msg_type = 0u;
        uint16_t plen = 0u;
        memcpy(&msg_type, msg + 4u, sizeof(uint32_t));
        if (msg_type == 1u) {
            memcpy(&plen, msg + 26u, sizeof(uint16_t));
        } else if (msg_type == 2u) {
            memcpy(&plen, msg + 8u, sizeof(uint16_t));
        }
        uint32_t mid_low = msg_id & 0x0FFFFFFFu;
        if (plen >= 4u && mid_low != 0x0061003Cu && mid_low != 0x0058003Fu &&
            mid_low != 0x008F013Au && mid_low != 0x008F0139u) {
            zvp_probe_rec_t rec;
            memset(&rec, 0, sizeof(rec));
            rec.kind = (uint8_t)ZVP_PROBE_DFLOW_SEND;
            rec.lr   = (uintptr_t)__builtin_return_address(0);
            rec.u0   = msg_id;
            rec.u1   = (msg_type << 16) | (uint32_t)plen;
            size_t cpy = (plen < sizeof(rec.raw)) ? (size_t)plen : sizeof(rec.raw);
            memcpy(rec.raw, msg + 16u, cpy);
            probe_push(&rec);
        }
    }
    return fn ? fn(self, msg_id, msg) : -1;
}

int _ZN16DataflowInfraPin4SendEmtm(void *self, unsigned long a1, unsigned short a2, unsigned long a3)
{
    static fn_dflow_pin_send_t real_fn = NULL;
    fn_dflow_pin_send_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_dflow_pin_send_t)dlsym(RTLD_NEXT, "_ZN16DataflowInfraPin4SendEmtm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(self, a1, a2, a3) : -1;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_PIN_SEND;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)a1;
        rec.u1   = (uint32_t)a2;
        rec.u2   = (uint32_t)a3;
        rec.rc   = rc;
        probe_push(&rec);
    }
    return rc;
}

int _ZN7MprCtrl16GetStillDataInfoEiiiiiiPN12MprCtrlStill13StillDataInfoE(
    int a0, int a1, int a2, int a3, int a4, int a5, uint8_t *out_info)
{
    static fn_get_still_data_info_t real_fn = NULL;
    fn_get_still_data_info_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_get_still_data_info_t)dlsym(RTLD_NEXT, "_ZN7MprCtrl16GetStillDataInfoEiiiiiiPN12MprCtrlStill13StillDataInfoE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(a0, a1, a2, a3, a4, a5, out_info) : -1;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind  = (uint8_t)ZVP_PROBE_GET_DATA_INFO;
        rec.lr    = (uintptr_t)__builtin_return_address(0);
        rec.u0    = (uint32_t)a0;
        rec.u1    = (uint32_t)a1;
        rec.u2    = (uint32_t)a2;
        rec.rec_w = (uint16_t)(a3 & 0xFFFF);
        rec.rec_h = (uint16_t)(a4 & 0xFFFF);
        rec.can_w = (uint16_t)(a5 & 0xFFFF);
        if (out_info) memcpy(rec.raw, out_info, 24u);
        rec.rc    = rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf11SetExifInfoEbPN3MPR10BufferAreaE(void *self, int flag, const uint32_t *buf_area)
{
    static fn_set_exif_info_t real_fn = NULL;
    fn_set_exif_info_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_exif_info_t)dlsym(RTLD_NEXT, "_ZN5MprIf11SetExifInfoEbPN3MPR10BufferAreaE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, flag, buf_area) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_SET_EXIF;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)flag;
        if (buf_area) {
            rec.u1 = buf_area[0];
            rec.u2 = buf_area[1];
        }
        rec.rc = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf11EncodeStillEPN3MPR10RegionInfoEPNS0_5T_LTMEPNS0_17StillMetaAreaInfoEPNS0_7JpgInfoE(
    void *self, const uint8_t *region, const uint8_t *ltm, const uint8_t *meta, const uint8_t *jpg)
{
    static fn_encode_still4_t real_fn = NULL;
    fn_encode_still4_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_encode_still4_t)dlsym(RTLD_NEXT, "_ZN5MprIf11EncodeStillEPN3MPR10RegionInfoEPNS0_5T_LTMEPNS0_17StillMetaAreaInfoEPNS0_7JpgInfoE");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, region, ltm, meta, jpg) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_ENCODE_STILL4;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        if (region) memcpy(rec.raw, region, 24u);
        if (jpg)    memcpy(rec.extra, jpg, 24u);
        rec.rc = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

unsigned long _ZN5MprIf22SetStillSizeMarginSyncEmmm(void *self, unsigned long a1, unsigned long a2, unsigned long a3)
{
    static fn_set_margin_sync_t real_fn = NULL;
    fn_set_margin_sync_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_set_margin_sync_t)dlsym(RTLD_NEXT, "_ZN5MprIf22SetStillSizeMarginSyncEmmm");
        if (fn) __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
    }
    unsigned long rc = fn ? fn(self, a1, a2, a3) : 1ul;
    if (!zvp_runtime_is_bypassed()) {
        zvp_probe_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.kind = (uint8_t)ZVP_PROBE_MARGIN_SYNC;
        rec.lr   = (uintptr_t)__builtin_return_address(0);
        rec.u0   = (uint32_t)a1;
        rec.u1   = (uint32_t)a2;
        rec.u2   = (uint32_t)a3;
        rec.rc   = (int32_t)rc;
        probe_push(&rec);
    }
    return rc;
}

static inline uint32_t valloc_hash_idx(uintptr_t ptr)
{
    return (((uint32_t)ptr * 2654435761u) >> 21) & (ZVP_VALLOC_MAP_CAP - 1u);
}

static void record_valloc_ptr(const void *ptr, uint32_t size)
{
    if ((uintptr_t)ptr < 0x10000u || size == 0u) {
        return;
    }
    uint32_t idx = valloc_hash_idx((uintptr_t)ptr);
    uint64_t val = ((uint64_t)size << 32) | (uint64_t)(uint32_t)(uintptr_t)ptr;
    __atomic_store_n(&s_valloc_map[idx], val, __ATOMIC_RELAXED);
}

static uint32_t lookup_valloc_size(const void *ptr)
{
    if ((uintptr_t)ptr < 0x10000u) {
        return 0u;
    }
    uint32_t idx = valloc_hash_idx((uintptr_t)ptr);
    uint64_t val = __atomic_load_n(&s_valloc_map[idx], __ATOMIC_RELAXED);
    if ((uint32_t)(val & 0xFFFFFFFFULL) == (uint32_t)(uintptr_t)ptr) {
        return (uint32_t)(val >> 32);
    }
    return 0u;
}

int osal_valloc_msg_wait(unsigned int msg_id, void **pp_msg, unsigned int size, unsigned int wait)
{
    fn_osal_valloc_t fn = __atomic_load_n(&s_real_osal_valloc, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osal_valloc_t)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
        if (fn) __atomic_store_n(&s_real_osal_valloc, fn, __ATOMIC_RELEASE);
    }
    int rc = fn ? fn(msg_id, pp_msg, size, wait) : -1;
    if (rc == 0 && pp_msg && *pp_msg) {
        record_valloc_ptr(*pp_msg, (uint32_t)size);
    }
    return rc;
}

int osal_snd_msg(unsigned int msg_id, void *msg)
{
    fn_osal_c_msg_t fn = __atomic_load_n(&s_real_osal_snd, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_msg");
        if (fn) __atomic_store_n(&s_real_osal_snd, fn, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&s_osal_c_armed, __ATOMIC_ACQUIRE) &&
        !zvp_runtime_is_bypassed() &&
        (uintptr_t)msg >= 0x10000u) {
        uint32_t alloc_sz = lookup_valloc_size(msg);
        uint32_t rlen = (alloc_sz >= 4u) ? ((alloc_sz <= 48u) ? alloc_sz : 48u) : 16u;
        probe_on_osal_ex((uintptr_t)__builtin_return_address(0), "C_Snd", msg_id,
                         (const uint8_t *)msg, rlen, alloc_sz);
    }
    return fn ? fn(msg_id, msg) : -1;
}

int osal_snd_sync_msg(unsigned int msg_id, void *msg)
{
    fn_osal_c_msg_t fn = __atomic_load_n(&s_real_osal_sync, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_sync_msg");
        if (fn) __atomic_store_n(&s_real_osal_sync, fn, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&s_osal_c_armed, __ATOMIC_ACQUIRE) &&
        !zvp_runtime_is_bypassed() &&
        (uintptr_t)msg >= 0x10000u) {
        uint32_t alloc_sz = lookup_valloc_size(msg);
        uint32_t rlen = (alloc_sz >= 4u) ? ((alloc_sz <= 48u) ? alloc_sz : 48u) : 16u;
        probe_on_osal_ex((uintptr_t)__builtin_return_address(0), "C_Sync", msg_id,
                         (const uint8_t *)msg, rlen, alloc_sz);
    }
    return fn ? fn(msg_id, msg) : -1;
}

int osal_snd_sync_direct(unsigned int msg_id, void *msg)
{
    fn_osal_c_msg_t fn = __atomic_load_n(&s_real_osal_dir, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_sync_direct");
        if (fn) __atomic_store_n(&s_real_osal_dir, fn, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&s_osal_c_armed, __ATOMIC_ACQUIRE) &&
        !zvp_runtime_is_bypassed() &&
        (uintptr_t)msg >= 0x10000u) {
        uint32_t dlen = 0u;
        memcpy(&dlen, msg, sizeof(uint32_t));
        uint32_t cpy = (dlen >= 4u && dlen <= 48u) ? dlen : 48u;
        probe_on_osal_ex((uintptr_t)__builtin_return_address(0), "C_Dir", msg_id,
                         (const uint8_t *)msg, cpy, dlen);
    }
    return fn ? fn(msg_id, msg) : -1;
}

/* ============================================================================
 * UI 主线程地址解析与日志转储
 * ========================================================================== */

#define ZVP_MOD_CACHE_MAX 32u

typedef struct zvp_mod_span {
    uintptr_t start;
    uintptr_t end;
    uintptr_t base;
    char      name[32];
} zvp_mod_span_t;

static zvp_mod_span_t s_mods[ZVP_MOD_CACHE_MAX];
static uint32_t       s_mod_cnt = 0u;
static bool           s_mods_scanned = false;

static int mod_scan_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    if (!info || !info->dlpi_name || s_mod_cnt >= ZVP_MOD_CACHE_MAX) {
        return 0;
    }
    const char *base_name = strrchr(info->dlpi_name, '/');
    base_name = base_name ? (base_name + 1) : info->dlpi_name;
    if (!base_name[0]) {
        base_name = "im.elf";
    }
    if (!strstr(base_name, "libObj") &&
        !strstr(base_name, "libmpr") &&
        !strstr(base_name, "libBizFw") &&
        !strstr(base_name, "dataflowInfra") &&
        !strstr(base_name, "viewUnified") &&
        !strstr(base_name, "libosal") &&
        !strstr(base_name, "libavbbc") &&
        !strstr(base_name, "im.elf")) {
        return 0;
    }
    uintptr_t min_va = (uintptr_t)-1;
    uintptr_t max_va = 0u;
    for (int i = 0; i < (int)info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD) {
            uintptr_t s = (uintptr_t)info->dlpi_addr + (uintptr_t)ph->p_vaddr;
            uintptr_t e = s + (uintptr_t)ph->p_memsz;
            if (s < min_va) min_va = s;
            if (e > max_va) max_va = e;
        }
    }
    if (max_va > min_va) {
        zvp_mod_span_t *m = &s_mods[s_mod_cnt++];
        m->start = min_va;
        m->end   = max_va;
        m->base  = (uintptr_t)info->dlpi_addr;
        size_t nlen = strlen(base_name);
        if (nlen >= sizeof(m->name)) nlen = sizeof(m->name) - 1u;
        memcpy(m->name, base_name, nlen);
        m->name[nlen] = '\0';
    }
    return 0;
}

static void format_caller(uintptr_t addr, char *out, size_t cap)
{
    if (!s_mods_scanned) {
        s_mods_scanned = true;
        s_mod_cnt = 0u;
        dl_iterate_phdr(mod_scan_cb, NULL);
    }
    for (uint32_t i = 0u; i < s_mod_cnt; ++i) {
        if (addr >= s_mods[i].start && addr < s_mods[i].end) {
            uintptr_t off = addr - s_mods[i].base;
            size_t nlen = strlen(s_mods[i].name);
            if (nlen + 12u < cap) {
                memcpy(out, s_mods[i].name, nlen);
                out[nlen++] = '+';
                out[nlen++] = '0';
                out[nlen++] = 'x';
                const char *hex = "0123456789abcdef";
                bool started = false;
                for (int sh = 28; sh >= 0; sh -= 4) {
                    unsigned nib = (unsigned)((off >> sh) & 0xFu);
                    if (nib != 0u || started || sh == 0) {
                        started = true;
                        if (nlen + 1u < cap) out[nlen++] = hex[nib];
                    }
                }
                out[nlen] = '\0';
                return;
            }
        }
    }
    const char *hex = "0123456789abcdef";
    size_t pos = 0u;
    if (cap > 10u) {
        out[pos++] = '0';
        out[pos++] = 'x';
        for (int sh = 28; sh >= 0; sh -= 4) {
            out[pos++] = hex[(unsigned)((addr >> sh) & 0xFu)];
        }
        out[pos] = '\0';
    } else if (cap > 0u) {
        out[0] = '\0';
    }
}

static void format_hex48(const uint8_t *buf, size_t len, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0u;
    for (size_t i = 0u; i < len && pos + 3u < cap; ++i) {
        if (i > 0u && (i % 4u) == 0u) {
            out[pos++] = '_';
        }
        out[pos++] = hex[(buf[i] >> 4) & 0xFu];
        out[pos++] = hex[buf[i] & 0xFu];
    }
    out[pos] = '\0';
}

void zvp_capture_probe_arm_osal(void)
{
    if (__sync_bool_compare_and_swap(&s_osal_c_armed, 0, 1)) {
        zvp_logf("cap_probe OSAL armed (8192 hash slots ready)");
    }
}

void zvp_capture_probe_drain_logs(void)
{
    if (!__atomic_load_n(&s_osal_c_armed, __ATOMIC_RELAXED)) {
        if (!__atomic_load_n(&s_real_osal_valloc, __ATOMIC_RELAXED)) {
            fn_osal_valloc_t f = (fn_osal_valloc_t)dlsym(RTLD_NEXT, "osal_valloc_msg_wait");
            if (f) __atomic_store_n(&s_real_osal_valloc, f, __ATOMIC_RELEASE);
        }
        if (!__atomic_load_n(&s_real_osal_snd, __ATOMIC_RELAXED)) {
            fn_osal_c_msg_t f = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_msg");
            if (f) __atomic_store_n(&s_real_osal_snd, f, __ATOMIC_RELEASE);
        }
        if (!__atomic_load_n(&s_real_osal_sync, __ATOMIC_RELAXED)) {
            fn_osal_c_msg_t f = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_sync_msg");
            if (f) __atomic_store_n(&s_real_osal_sync, f, __ATOMIC_RELEASE);
        }
        if (!__atomic_load_n(&s_real_osal_dir, __ATOMIC_RELAXED)) {
            fn_osal_c_msg_t f = (fn_osal_c_msg_t)dlsym(RTLD_NEXT, "osal_snd_sync_direct");
            if (f) __atomic_store_n(&s_real_osal_dir, f, __ATOMIC_RELEASE);
        }
    }

    uint32_t head = __atomic_load_n(&s_ring_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&s_ring_tail, __ATOMIC_ACQUIRE);
    while (head != tail) {
        zvp_probe_rec_t rec = s_ring[head];
        head = (head + 1u) % ZVP_PROBE_RING_CAP;
        __atomic_store_n(&s_ring_head, head, __ATOMIC_RELEASE);
        __atomic_fetch_add(&s_stat_drained, 1u, __ATOMIC_RELAXED);

        char caller[48];
        char hexbuf[128];
        char exbuf[128];
        format_caller(rec.lr, caller, sizeof(caller));

        switch ((zvp_probe_kind_t)rec.kind) {
        case ZVP_PROBE_SET_IMG_INFO:
        case ZVP_PROBE_SET_DATA_ALL:
        case ZVP_PROBE_GET_IMG_INFO:
        case ZVP_PROBE_GET_DATA_ALL: {
            const char *name = "SetImgInfo";
            if (rec.kind == (uint8_t)ZVP_PROBE_SET_DATA_ALL) name = "SetDataAll";
            else if (rec.kind == (uint8_t)ZVP_PROBE_GET_IMG_INFO) name = "GetImgInfo";
            else if (rec.kind == (uint8_t)ZVP_PROBE_GET_DATA_ALL) name = "GetDataAll";
            zvp_logf("cap_probe %s [%s] rec=%ux%u can=%ux%u voff=(%u,%u) val=%ux%u tgt=%ux%u asp=%u att=%u cnt=%u site=%u cmp=%u ut=0x%x",
                     name, caller,
                     (unsigned)rec.rec_w, (unsigned)rec.rec_h,
                     (unsigned)rec.can_w, (unsigned)rec.can_h,
                     (unsigned)rec.voff_x, (unsigned)rec.voff_y,
                     (unsigned)rec.val_w, (unsigned)rec.val_h,
                     (unsigned)rec.tgt_w, (unsigned)rec.tgt_h,
                     (unsigned)rec.aspect, (unsigned)rec.attach, (unsigned)rec.content,
                     (unsigned)rec.data_site, (unsigned)rec.compress,
                     (unsigned)rec.u0);
            break;
        }
        case ZVP_PROBE_SET_REC_X:
            zvp_logf("cap_probe SetRecX [%s] rec_w=%u", caller, (unsigned)rec.rec_w);
            break;
        case ZVP_PROBE_CHK_CANVAS:
            zvp_logf("cap_probe CheckCanvasStill [%s] %ux%u -> %d",
                     caller, (unsigned)rec.val_w, (unsigned)rec.val_h, (int)rec.rc);
            break;
        case ZVP_PROBE_GET_CANVAS:
            zvp_logf("cap_probe GetCanvas [%s] idx=%u asp=%u -> %ux%u rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.aspect,
                     (unsigned)rec.can_w, (unsigned)rec.can_h, (int)rec.rc);
            break;
        case ZVP_PROBE_ENCODE_STILL:
            zvp_logf("cap_probe EncodeStill [%s] p2=0x%x p3=%ux%u p4=%ux%u p5=%ux%u rc=%d",
                     caller, (unsigned)rec.u0,
                     (unsigned)rec.rec_w, (unsigned)rec.rec_h,
                     (unsigned)rec.can_w, (unsigned)rec.can_h,
                     (unsigned)rec.val_w, (unsigned)rec.val_h, (int)rec.rc);
            break;
        case ZVP_PROBE_CHK_STILL_SZ:
            zvp_logf("cap_probe CheckStillSize [%s] mode=%u %ux%u ok=%u st=%d",
                     caller, (unsigned)rec.u0,
                     (unsigned)rec.val_w, (unsigned)rec.val_h,
                     (unsigned)rec.aspect, (int)rec.rc);
            break;
        case ZVP_PROBE_WRITE_STILL:
            format_hex48(rec.raw, 24u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe WriteStill [%s] cnt=%u p12/16/20=(%u,%u,%u) p24=0x%x info=(%u,%u,%u,%u,%u,%u,%u,0x%x) rc=%d raw=%s p48=%s",
                     caller, (unsigned)rec.u0,
                     (unsigned)rec.rec_w, (unsigned)rec.rec_h, (unsigned)rec.can_w,
                     (unsigned)rec.u1,
                     (unsigned)rec.val_w, (unsigned)rec.val_h,
                     (unsigned)rec.tgt_w, (unsigned)rec.tgt_h,
                     (unsigned)rec.voff_x, (unsigned)rec.voff_y, (unsigned)rec.can_h,
                     (unsigned)rec.u2,
                     (int)rec.rc, hexbuf, exbuf);
            break;
        case ZVP_PROBE_MAKE_STILL:
            format_hex48(rec.raw, 48u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe MakeStill [%s] info=(%u,%u,%u,%u,%u,%u) rc=%d p48=%s i24=%s",
                     caller, (unsigned)rec.val_w, (unsigned)rec.val_h,
                     (unsigned)rec.tgt_w, (unsigned)rec.tgt_h,
                     (unsigned)rec.voff_x, (unsigned)rec.voff_y,
                     (int)rec.rc, hexbuf, exbuf);
            break;
        case ZVP_PROBE_STILL_BUFS:
            format_hex48(rec.raw, 48u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe StillBufs [%s] cnt=%u a=%s b=%s",
                     caller, (unsigned)rec.u0, hexbuf, exbuf);
            break;
        case ZVP_PROBE_STILL_META:
            format_hex48(rec.raw, 48u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe StillMeta [%s] cnt=%u m20=%s m50=%s",
                     caller, (unsigned)rec.u0, hexbuf, exbuf);
            break;
        case ZVP_PROBE_FLUSH_WRITE:
            format_hex48(rec.raw, 48u, hexbuf, sizeof(hexbuf));
            zvp_logf("cap_probe FlushWrite [%s] a2=0x%x rc=%d p48=%s",
                     caller, (unsigned)rec.u0, (int)rec.rc, hexbuf);
            break;
        case ZVP_PROBE_FILE_OPEN: {
            char pbuf[48];
            memcpy(pbuf, rec.raw, sizeof(pbuf));
            pbuf[sizeof(pbuf) - 1u] = '\0';
            zvp_logf("cap_probe MprOpen [%s] mode=%u hdl=0x%x rc=%d path=%s",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (int)rec.rc, pbuf);
            break;
        }
        case ZVP_PROBE_FILE_CLOSE:
            zvp_logf("cap_probe MprClose [%s] hdl=0x%x b=%u rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (int)rec.rc);
            break;
        case ZVP_PROBE_PLAY_IMAGE:
            format_hex48(rec.raw, 48u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe PlayStillImg [%s] media=%u rc=%d in=%s out=%s",
                     caller, (unsigned)rec.u0, (int)rec.rc, hexbuf, exbuf);
            break;
        case ZVP_PROBE_GET_REC_INFO:
            format_hex48(rec.raw, 24u, hexbuf, sizeof(hexbuf));
            zvp_logf("cap_probe GetStillRecInfo [%s] p1=%d p2=%d rc=%d raw=%s",
                     caller, (int)rec.u0, (int)rec.u1, (int)rec.rc, hexbuf);
            break;
        case ZVP_PROBE_SET_TGT_SZ:
            zvp_logf("cap_probe SetTgtStillSz [%s] a1=0x%x type=%u a3=0x%x rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        case ZVP_PROBE_SET_STILL_SZ:
            zvp_logf("cap_probe SetStillSize [%s] a1=0x%x p0=0x%x a3=0x%x rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        case ZVP_PROBE_SET_REC_ASP:
            zvp_logf("cap_probe SetRecAspect [%s] p0=0x%x p1=0x%x a2=0x%x rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        case ZVP_PROBE_OSAL_MSG: {
            uint16_t cmd = (uint16_t)(rec.u1 >> 16);
            uint16_t len = (uint16_t)(rec.u1 & 0xFFFFu);
            format_hex48(rec.raw, (len < 48u) ? len : 48u, hexbuf, sizeof(hexbuf));
            zvp_logf("cap_probe OSAL[%s] [%s] mid=0x%08x cmd=0x%04x len=%u/%u d=%s",
                     rec.tag, caller, (unsigned)rec.u0, (unsigned)cmd,
                     (unsigned)len, (unsigned)rec.u2, hexbuf);
            break;
        }
        case ZVP_PROBE_DFLOW_SEND: {
            uint16_t mtype = (uint16_t)(rec.u1 >> 16);
            uint16_t plen  = (uint16_t)(rec.u1 & 0xFFFFu);
            format_hex48(rec.raw, (plen < 48u) ? plen : 48u, hexbuf, sizeof(hexbuf));
            zvp_logf("cap_probe DflowSend [%s] mid=0x%08x type=%u len=%u d=%s",
                     caller, (unsigned)rec.u0, (unsigned)mtype, (unsigned)plen, hexbuf);
            break;
        }
        case ZVP_PROBE_PIN_SEND:
            zvp_logf("cap_probe PinSend [%s] a1=0x%x a2=0x%x a3=0x%x rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        case ZVP_PROBE_GET_DATA_INFO:
            format_hex48(rec.raw, 24u, hexbuf, sizeof(hexbuf));
            zvp_logf("cap_probe GetStillDataInfo [%s] a=(%d,%d,%d,%u,%u,%u) rc=%d out=%s",
                     caller, (int)rec.u0, (int)rec.u1, (int)rec.u2,
                     (unsigned)rec.rec_w, (unsigned)rec.rec_h, (unsigned)rec.can_w,
                     (int)rec.rc, hexbuf);
            break;
        case ZVP_PROBE_SET_EXIF:
            zvp_logf("cap_probe SetExifInfo [%s] flag=%u buf=(0x%x,0x%x) rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        case ZVP_PROBE_ENCODE_STILL4:
            format_hex48(rec.raw, 24u, hexbuf, sizeof(hexbuf));
            format_hex48(rec.extra, 24u, exbuf, sizeof(exbuf));
            zvp_logf("cap_probe EncodeStill4 [%s] rc=%d reg=%s jpg=%s",
                     caller, (int)rec.rc, hexbuf, exbuf);
            break;
        case ZVP_PROBE_MARGIN_SYNC:
            zvp_logf("cap_probe SetMarginSync [%s] a=(0x%x,0x%x,0x%x) rc=%d",
                     caller, (unsigned)rec.u0, (unsigned)rec.u1, (unsigned)rec.u2, (int)rec.rc);
            break;
        default:
            break;
        }
    }
}
