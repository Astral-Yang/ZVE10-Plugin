#define _GNU_SOURCE
#include "zvp_runtime.h"
#include "zvp_lut_blobs.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* 强制将 dlsym 锁定到 GLIBC_2.4，防止新工具链引入 GLIBC_2.34 符号 */
#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

/* ============================================================================
 * 日志与基础时间辅助（严禁使用 glibc >= 2.12 高版本符号或 64-bit time_t）
 * ========================================================================== */

#define ZVP_LOG_PATH       "/tmp/zve10plugin.log"
#define ZVP_LOG_MAX_BYTES  (256u * 1024u)
#define ZVP_W0_LOG_PATH    "/tmp/zvp_w0.log"
#define ZVP_W0_LOG_MAX_BYTES (128u * 1024u)

static uint64_t s_last_mono_ms = 0;

static void append_and_fsync_log(const char *path, uint32_t max_bytes,
                                 const char *buf, size_t total, int do_fsync)
{
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    int probe_fd = open(path, O_RDONLY);
    if (probe_fd >= 0) {
        off_t sz = lseek(probe_fd, 0, SEEK_END);
        close(probe_fd);
        if (sz >= (off_t)max_bytes) {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return;
    }
    ssize_t wr = write(fd, buf, total);
    (void)wr;
    if (do_fsync) {
        (void)fsync(fd);
    }
    close(fd);
}

uint64_t zvp_monotonic_ms(void)
{
    uint64_t now = 0;
#if defined(__arm__) && defined(SYS_clock_gettime)
    int32_t ts[2] = { 0, 0 }; /* 32-bit ARM EABI struct timespec */
    if (syscall(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, ts) == 0) {
        now = (uint64_t)(uint32_t)ts[0] * 1000ull + (uint64_t)(uint32_t)ts[1] / 1000000ull;
    } else
#endif
    {
        int32_t tv[2] = { 0, 0 };
        if (syscall(SYS_gettimeofday, tv, NULL) == 0) {
            now = (uint64_t)(uint32_t)tv[0] * 1000ull + (uint64_t)(uint32_t)tv[1] / 1000ull;
        }
    }
    if (now < s_last_mono_ms) {
        now = s_last_mono_ms;
    } else {
        s_last_mono_ms = now;
    }
    return now;
}

void zvp_logf(const char *fmt, ...)
{
    char buf[384];
    int prefix_len = snprintf(buf, sizeof(buf), "[zvp %llu] ",
                              (unsigned long long)zvp_monotonic_ms());
    if (prefix_len < 0) {
        prefix_len = 0;
    } else if ((size_t)prefix_len >= sizeof(buf)) {
        prefix_len = (int)sizeof(buf) - 1;
    }

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(buf + prefix_len,
                             sizeof(buf) - (size_t)prefix_len,
                             fmt, ap);
    va_end(ap);

    size_t total = (size_t)prefix_len + ((body_len > 0) ? (size_t)body_len : 0u);
    if (total >= sizeof(buf)) {
        total = sizeof(buf) - 1u;
    }
    if (total == 0u || buf[total - 1u] != '\n') {
        if (total + 1u < sizeof(buf)) {
            buf[total++] = '\n';
        } else {
            buf[sizeof(buf) - 2u] = '\n';
            total = sizeof(buf) - 1u;
        }
    }
    buf[total] = '\0';

    int is_crit = (strstr(buf, "[craw") != NULL || strstr(buf, "[w0_") != NULL ||
                   strstr(buf, "ui craw") != NULL || strstr(buf, "看门狗") != NULL ||
                   strstr(buf, "zve10plugin") != NULL);
    append_and_fsync_log(ZVP_LOG_PATH, ZVP_LOG_MAX_BYTES, buf, total, is_crit);
    if (is_crit) {
        append_and_fsync_log(ZVP_W0_LOG_PATH, ZVP_W0_LOG_MAX_BYTES, buf, total, 1);
    }
}

/* ============================================================================
 * 模块定位与页对齐内存热补丁（基于 mprotect + __builtin___clear_cache）
 * ========================================================================== */

struct dl_find_ctx {
    const char *target_name;
    uintptr_t found_base;
};

static int dl_find_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    struct dl_find_ctx *ctx = (struct dl_find_ctx *)data;
    if (!info || !info->dlpi_name || !ctx || !ctx->target_name) {
        return 0;
    }
    if (strstr(info->dlpi_name, ctx->target_name) != NULL) {
        ctx->found_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static uintptr_t plat_find_module_base(const char *soname)
{
    if (!soname) {
        return 0;
    }
    struct dl_find_ctx ctx;
    ctx.target_name = soname;
    ctx.found_base = 0;
    dl_iterate_phdr(dl_find_cb, &ctx);
    return ctx.found_base;
}

static int plat_read_mem(uintptr_t addr, void *out_buf, size_t len)
{
    if (!addr || !out_buf || len == 0) {
        return -1;
    }
    memcpy(out_buf, (const void *)addr, len);
    return 0;
}

static int plat_write_mem(uintptr_t addr, const void *buf, size_t len)
{
    if (!addr || !buf || len == 0) {
        return -1;
    }
    const size_t page_sz = 4096u;
    uintptr_t start = addr & ~(page_sz - 1u);
    uintptr_t end = (addr + len + page_sz - 1u) & ~(page_sz - 1u);
    size_t span = (size_t)(end - start);

    if (mprotect((void *)start, span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        if (mprotect((void *)start, span, PROT_READ | PROT_WRITE) != 0) {
            return -1;
        }
    }
    memcpy((void *)addr, buf, len);
    __builtin___clear_cache((char *)addr, (char *)(addr + len));
    /* 恢复 RX（如为纯数据段也允许先保留 R|X 或恢复为 R|X） */
    (void)mprotect((void *)start, span, PROT_READ | PROT_EXEC);
    return 0;
}

/* ============================================================================
 * Sony libbackup / libInfraFsys 平台适配层
 * ========================================================================== */

static zvp_fn_backup_read_t      s_backup_read      = NULL;
static zvp_fn_backup_write_t     s_backup_write     = NULL;
static zvp_fn_fsys_mounts_t      s_fsys_mounts      = NULL;
static zvp_fn_fsys_open_t        s_fsys_open        = NULL;
static zvp_fn_fsys_close_t       s_fsys_close       = NULL;
typedef int (*zvp_fn_fsys_find_first_t)(const char *pattern, uint32_t attr_mask, void *find_info);
static zvp_fn_fsys_find_first_t  s_fsys_find_first  = NULL;

static int plat_backup_read(uint32_t bkid, void *buf)
{
    if (!s_backup_read) {
        s_backup_read = (zvp_fn_backup_read_t)dlsym(RTLD_DEFAULT, "Backup_read");
    }
    if (!s_backup_read) {
        return -1;
    }
    return s_backup_read(bkid, buf);
}

static int plat_backup_write(uint32_t subsys, uint32_t bkid, const void *buf)
{
    if (!s_backup_write) {
        s_backup_write = (zvp_fn_backup_write_t)dlsym(RTLD_DEFAULT, "Backup_write");
    }
    if (!s_backup_write) {
        return -1;
    }
    return s_backup_write(subsys, bkid, buf);
}

static uint32_t plat_get_mount_drives(void)
{
    if (!s_fsys_mounts) {
        s_fsys_mounts = (zvp_fn_fsys_mounts_t)dlsym(RTLD_DEFAULT, "Fsys_GetMountDrives");
    }
    if (s_fsys_mounts) {
        return s_fsys_mounts();
    }
    /* 若运行于未链接 libInfraFsys 的测试/极早环境，检查 POSIX 挂载备选路径 */
    int fd = open("/mnt/sd", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return ZVP_FSYS_DRIVE_B_BIT;
    }
    return 0;
}

static int plat_file_exists(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    /* 任意 PrFILE2 盘符路径 ("A:/" .. "Z:/") 均走 Sony Fsys_Open / Fsys_FindFirst */
    if ((path[0] >= 'A' && path[0] <= 'Z') && path[1] == ':' && path[2] == '/') {
        if (!s_fsys_open) {
            s_fsys_open = (zvp_fn_fsys_open_t)dlsym(RTLD_DEFAULT, "Fsys_Open");
        }
        if (!s_fsys_close) {
            s_fsys_close = (zvp_fn_fsys_close_t)dlsym(RTLD_DEFAULT, "Fsys_Close");
        }
        if (!s_fsys_find_first) {
            s_fsys_find_first = (zvp_fn_fsys_find_first_t)dlsym(RTLD_DEFAULT, "Fsys_FindFirst");
        }
        if (s_fsys_open && s_fsys_close) {
            int handle = -1;
            int rc = s_fsys_open(path, &handle, 0);
            if (rc == 0 && handle >= 0) {
                s_fsys_close(handle);
                return 1;
            }
        }
        if (s_fsys_find_first) {
            uint8_t *fi = (uint8_t *)calloc(1u, 2088u);
            if (fi) {
                int frc = s_fsys_find_first(path, 0x80u, fi);
                free(fi);
                if (frc == 0) {
                    return 1;
                }
            }
        }
        return 0;
    }
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

static void plat_sleep_ms(uint32_t ms)
{
    usleep((useconds_t)ms * 1000u);
}

int zvp_bridge_init_platform(zvp_wifi5g_platform_ops_t *out_wifi_ops,
                             zvp_gate_ops_t *out_gate_ops)
{
    if (out_wifi_ops) {
        out_wifi_ops->find_module_base = plat_find_module_base;
        out_wifi_ops->read_mem         = plat_read_mem;
        out_wifi_ops->write_mem        = plat_write_mem;
        out_wifi_ops->backup_read      = plat_backup_read;
        out_wifi_ops->backup_write     = plat_backup_write;
    }
    if (out_gate_ops) {
        out_gate_ops->get_mount_drives = plat_get_mount_drives;
        out_gate_ops->file_exists      = plat_file_exists;
        out_gate_ops->sleep_ms         = plat_sleep_ms;
    }
    return 0;
}

/* ============================================================================
 * Sony libObj.so 原生 GEN_CacheInsImg + RGBA4444 画布与 onAnimate 挂钩
 * ========================================================================== */

typedef void *(*fn_ws_get_instance_t)(void);
typedef void *(*fn_ws_layer_op_t)(void *ws, const uint32_t *core_id);
typedef void  (*fn_layer_bool_t)(void *layer, bool val);
typedef void  (*fn_layer_void_t)(void *layer);

typedef void  (*fn_cacheimg_ctor_t)(void *self);
typedef void  (*fn_cacheimg_size_t)(void *self, float w, float h);
typedef void  (*fn_cacheimg_clear_t)(void *self, uint32_t ccolor_packed);

typedef void  (*fn_widget_set_layer_t)(void *w, const uint32_t *core_id);
typedef void  (*fn_widget_set_pos_t)(void *w, const float xy[2]);
typedef void  (*fn_widget_set_size_t)(void *w, float w_px, float h_px);
typedef void  (*fn_widget_void_t)(void *w);
typedef void  (*fn_widget_ctor_t)(void *w);
typedef void  (*fn_widget_set_anim_t)(void *w, bool en);
typedef void  (*fn_widget_set_passive_t)(void *w, bool en);
typedef uint8_t (*fn_widget_get_arena_t)(void *w);

typedef void *(*fn_resmgr_get_instance_t)(void);
typedef int   (*fn_resmgr_create_img_t)(const void *param, void *img_data);
typedef void  (*fn_create_param_ctor_t)(void *param, uint16_t w, uint16_t h, int fmt);
typedef void  (*fn_create_param_arena_t)(void *param, uint8_t arena_id);

typedef void *(*fn_img_sys_get_t)(void *img_data);
typedef uint32_t (*fn_img_get_resid_t)(const void *img_data);
typedef void *(*fn_gfx_get_internal_t)(void *gfx_img);

typedef int   (*fn_grm_lock_bits_t)(void *bmp, int plane, void **out_bits);
typedef int   (*fn_grm_unlock_bits_t)(void *bmp);
typedef int   (*fn_grm_row_pixels_t)(void *bmp, uint16_t *out_px);

typedef void  (*fn_sprite_set_img_t)(void *sprite, uint32_t res_id, uint32_t flag);
typedef void  (*fn_sprite_invalidate_t)(void *sprite);
typedef uint32_t (*fn_core_id_gen_t)(const char *name);

typedef struct zvp_libobj_api {
    fn_ws_get_instance_t    ws_get_instance;
    fn_ws_layer_op_t        ws_get_layer;
    fn_ws_layer_op_t        ws_create_layer;
    fn_layer_bool_t         layer_set_active;
    fn_layer_bool_t         layer_set_clear;
    fn_layer_void_t         layer_move_last;

    fn_cacheimg_ctor_t      cacheimg_ctor;
    fn_cacheimg_size_t      cacheimg_set_size;
    fn_cacheimg_clear_t     cacheimg_clear;

    fn_widget_set_layer_t   widget_set_layer;
    fn_widget_set_pos_t     widget_set_pos;
    fn_widget_set_size_t    widget_set_size;
    fn_widget_void_t        widget_move_last;
    fn_widget_ctor_t        widget_ctor;
    fn_widget_set_anim_t    widget_set_anim;
    fn_widget_set_passive_t widget_set_passive;
    fn_widget_get_arena_t   widget_get_arena;
    uintptr_t               addr_widgetbase_on_animate;

    fn_resmgr_get_instance_t resmgr_get_instance;
    fn_resmgr_create_img_t   resmgr_create_img;
    fn_create_param_ctor_t   create_param_ctor;
    fn_create_param_arena_t  create_param_arena;

    fn_img_sys_get_t        img_sys_get;
    fn_img_get_resid_t      img_get_resid;
    fn_gfx_get_internal_t   gfx_get_internal;

    fn_grm_lock_bits_t      grm_lock_bits;
    fn_grm_unlock_bits_t    grm_unlock_bits;
    fn_grm_row_pixels_t     grm_row_pixels;

    fn_sprite_set_img_t     sprite_set_img;
    fn_sprite_invalidate_t  sprite_invalidate;
    fn_core_id_gen_t        core_id_gen;
} zvp_libobj_api_t;

static zvp_libobj_api_t s_obj;

static void zvp_ticker_on_animate_trampoline(void *self)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (rt && rt->bridge.orig_on_animate) {
        rt->bridge.orig_on_animate(self);
    }
    zvp_runtime_ui_frame_tick();
}

static int resolve_libobj_symbols(zvp_native_bridge_t *br)
{
    if (br->symbols_resolved) {
        return 0;
    }

    uintptr_t libobj_base = plat_find_module_base("libObj.so");
    if (!libobj_base) {
        return -1;
    }

#define RESOLVE_REQ(field, type, sym) do { \
    s_obj.field = (type)dlsym(RTLD_DEFAULT, sym); \
    if (!s_obj.field) { \
        zvp_logf("dlsym 缺失关键符号: %s", sym); \
        return -1; \
    } \
} while (0)

#define RESOLVE_OPT(field, type, sym) do { \
    s_obj.field = (type)dlsym(RTLD_DEFAULT, sym); \
} while (0)

    RESOLVE_REQ(ws_get_instance, fn_ws_get_instance_t, "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    RESOLVE_REQ(ws_get_layer, fn_ws_layer_op_t, "_ZNK2ux6wgtsys12WidgetSystem8getLayerERKNS_4core2IDE");
    RESOLVE_REQ(ws_create_layer, fn_ws_layer_op_t, "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
    RESOLVE_REQ(layer_set_active, fn_layer_bool_t, "_ZN2ux6wgtsys5Layer9setActiveEb");
    RESOLVE_OPT(layer_set_clear, fn_layer_bool_t, "_ZN2ux6wgtsys5Layer8setClearEb");
    RESOLVE_OPT(layer_move_last, fn_layer_void_t, "_ZN2ux6wgtsys9LayerBase8moveLastEv");

    RESOLVE_REQ(cacheimg_ctor, fn_cacheimg_ctor_t, "_ZN15GEN_CacheInsImgC1Ev");
    RESOLVE_REQ(cacheimg_set_size, fn_cacheimg_size_t, "_ZN15GEN_CacheInsImg7setSizeEff");
    RESOLVE_OPT(cacheimg_clear, fn_cacheimg_clear_t, "_ZN15GEN_CacheInsImg10clearImageEN6stella6CColorE");

    RESOLVE_REQ(widget_set_layer, fn_widget_set_layer_t, "_ZN2ux6wgtsys6Widget10setLayerIDERKNS_4core2IDE");
    RESOLVE_REQ(widget_set_pos, fn_widget_set_pos_t, "_ZN2ux6wgtsys6Widget11setLocalPosERKNS_4core7Vector2E");
    RESOLVE_REQ(widget_set_size, fn_widget_set_size_t, "_ZN2ux6wgtsys6Widget7setSizeEff");
    RESOLVE_OPT(widget_move_last, fn_widget_void_t, "_ZN2ux6wgtsys6Widget8moveLastEv");
    RESOLVE_REQ(widget_ctor, fn_widget_ctor_t, "_ZN2ux6wgtsys6WidgetC1Ev");
    RESOLVE_REQ(widget_set_anim, fn_widget_set_anim_t, "_ZN2ux6wgtsys10WidgetBase10setAnimateEb");
    RESOLVE_OPT(widget_set_passive, fn_widget_set_passive_t, "_ZN2ux6wgtsys6Widget16setPassiveRegionEb");
    RESOLVE_OPT(widget_get_arena, fn_widget_get_arena_t, "_ZN13AppWidgetBase10getArenaIDEv");

    void *on_anim_ptr = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys10WidgetBase9onAnimateEv");
    s_obj.addr_widgetbase_on_animate = (uintptr_t)on_anim_ptr;

    RESOLVE_OPT(resmgr_get_instance, fn_resmgr_get_instance_t, "_ZN2ux6resmgr15ResourceManager15sys_getInstanceEv");
    RESOLVE_REQ(resmgr_create_img, fn_resmgr_create_img_t, "_ZN2ux6resmgr15ResourceManager11createImageERKN5libra16CreateImageParamEPNS2_9ImageDataE");
    RESOLVE_REQ(create_param_ctor, fn_create_param_ctor_t, "_ZN5libra16CreateImageParamC1Etti");
    RESOLVE_OPT(create_param_arena, fn_create_param_arena_t, "_ZN5libra16CreateImageParam10setArenaIDEh");

    RESOLVE_REQ(img_sys_get, fn_img_sys_get_t, "_ZN5libra9ImageData16sys_getImageDataEv");
    RESOLVE_REQ(img_get_resid, fn_img_get_resid_t, "_ZNK5libra9ImageData8getResIDEv");
    RESOLVE_REQ(gfx_get_internal, fn_gfx_get_internal_t, "_ZN2ux4core13GraphicsImage21sys_getInternalObjectEPv");

    RESOLVE_REQ(grm_lock_bits, fn_grm_lock_bits_t, "GRM_bitmapWriteLockBits");
    RESOLVE_REQ(grm_unlock_bits, fn_grm_unlock_bits_t, "GRM_bitmapWriteUnlockBits");
    RESOLVE_OPT(grm_row_pixels, fn_grm_row_pixels_t, "GRM_bitmapGetRowPixelCount");

    RESOLVE_OPT(core_id_gen, fn_core_id_gen_t, "_ZN2ux4core2ID5genIDEPKc");

#undef RESOLVE_REQ
#undef RESOLVE_OPT

    /* 校验并绑定 Sprite 内部未导出但已反汇编定位的函数指针 */
    uintptr_t s_set_addr = libobj_base + (ZVP_OFF_SPRITE_SET_IMG & ~1u);
    uintptr_t s_inv_addr = libobj_base + (ZVP_OFF_SPRITE_INVALIDATE & ~1u);
    /* 0x624dd4: cmp.w r1, #-1 (F1 B1 ...), 0x624eb4: push {r4,r5,r6,r7,lr} (B5 F0) */
    if (*(const uint16_t *)s_set_addr == 0xF1B1u) {
        s_obj.sprite_set_img = (fn_sprite_set_img_t)(s_set_addr | 1u);
    }
    if (*(const uint16_t *)s_inv_addr == 0xB5F0u) {
        s_obj.sprite_invalidate = (fn_sprite_invalidate_t)(s_inv_addr | 1u);
    }

    zvp_logf("libObj 符号解析完成: base=0x%lx sprite_inv=%p onAnimate=%p",
             (unsigned long)libobj_base,
             (void *)s_obj.sprite_invalidate,
             (void *)s_obj.addr_widgetbase_on_animate);

    br->symbols_resolved = true;
    return 0;
}

int zvp_bridge_ensure_ticker(zvp_native_bridge_t *br)
{
    if (!br) {
        return -1;
    }
    if (br->ticker_ready) {
        return 0;
    }
    if (br->ticker_failed) {
        return -1;
    }
    if (resolve_libobj_symbols(br) != 0) {
        return -1;
    }

    void *ws = s_obj.ws_get_instance();
    if (!ws) {
        return -1;
    }

    uint32_t tlid = s_obj.core_id_gen ? s_obj.core_id_gen("ZVP_TICKER_LYR") : 0x9A557E11u;
    br->ticker_layer_id = tlid;
    void *tlayer = s_obj.ws_get_layer(ws, &tlid);
    if (!tlayer) {
        tlayer = s_obj.ws_create_layer(ws, &tlid);
    }
    if (!tlayer) {
        zvp_logf("createLayer(ticker) 失败 (tlid=0x%08x)", (unsigned)tlid);
        br->ticker_failed = true;
        return -1;
    }
    br->ticker_layer = tlayer;
    if (s_obj.layer_set_clear) {
        s_obj.layer_set_clear(tlayer, false);
    }

    void *tk = calloc(1, 0x300u);
    if (!tk) {
        br->ticker_failed = true;
        return -1;
    }
    s_obj.widget_ctor(tk);

    uintptr_t *orig_vt = *(uintptr_t **)tk;
    if (!orig_vt) {
        free(tk);
        br->ticker_failed = true;
        return -1;
    }

    /* 保留前 2 个负偏移 RTTI/offset-to-top 槽位（与 A6400 trial3 一致） */
    memcpy(br->cloned_vtable, orig_vt - 2, sizeof(br->cloned_vtable));
    int slot = -1;
    if (s_obj.addr_widgetbase_on_animate != 0u) {
        for (int i = 0; i < 48; ++i) {
            if (orig_vt[i] == s_obj.addr_widgetbase_on_animate) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || slot >= 64) {
        slot = (int)ZVP_WIDGETBASE_ONANIMATE_SLOT_DEFAULT;
    }
    br->on_animate_slot = slot;
    br->orig_on_animate = (void (*)(void *))orig_vt[slot];
    br->cloned_vtable[slot + 2] = (uintptr_t)zvp_ticker_on_animate_trampoline;
    *(uintptr_t **)tk = &br->cloned_vtable[2];

    if (s_obj.widget_set_passive) {
        s_obj.widget_set_passive(tk, true);
    }
    s_obj.widget_set_layer(tk, &tlid);
    const float xy[2] = { 0.0f, 0.0f };
    s_obj.widget_set_pos(tk, xy);
    s_obj.widget_set_size(tk, 0.0f, 0.0f);
    s_obj.layer_set_active(tlayer, true);
    s_obj.widget_set_anim(tk, true);

    br->ticker_widget = tk;
    br->ticker_ready = true;
    zvp_logf("常驻 ticker 心跳挂载完成: tlid=0x%08x slot=%d orig=%p",
             (unsigned)tlid, slot, (void *)br->orig_on_animate);
    return 0;
}

void zvp_bridge_stop_ticker(zvp_native_bridge_t *br)
{
    if (!br || !br->ticker_ready) {
        return;
    }
    if (br->ticker_widget && s_obj.widget_set_anim) {
        s_obj.widget_set_anim(br->ticker_widget, false);
    }
    if (br->ticker_layer && s_obj.layer_set_active) {
        s_obj.layer_set_active(br->ticker_layer, false);
    }
}

int zvp_bridge_ensure_canvas(zvp_native_bridge_t *br)
{
    if (!br) {
        return -1;
    }
    if (br->canvas_ready) {
        return 0;
    }
    if (br->canvas_failed) {
        return -1;
    }
    if (resolve_libobj_symbols(br) != 0) {
        return -1;
    }

    void *ws = s_obj.ws_get_instance();
    if (!ws) {
        /* 相机刚启动时 WidgetSystem 尚未就绪，稍后重试 */
        return -1;
    }

    /* 1. 创建或获取专属全屏叠加层 */
    uint32_t lid = s_obj.core_id_gen ? s_obj.core_id_gen("ZVP_OVERLAY_LYR") : 0x9A557E10u;
    br->layer_id = lid;
    void *layer = s_obj.ws_get_layer(ws, &lid);
    if (!layer) {
        layer = s_obj.ws_create_layer(ws, &lid);
    }
    if (!layer) {
        zvp_logf("createLayer 失败 (lid=0x%08x)", (unsigned)lid);
        br->canvas_failed = true;
        return -1;
    }
    br->layer_obj = layer;
    if (s_obj.layer_set_clear) {
        s_obj.layer_set_clear(layer, false);
    }
    s_obj.layer_set_active(layer, false);

    /* 2. 分配并构造 GEN_CacheInsImg 画布实例 */
    void *cw = calloc(1, ZVP_CACHEIMG_ALLOC_SIZE);
    if (!cw) {
        br->canvas_failed = true;
        return -1;
    }
    s_obj.cacheimg_ctor(cw);

    /* 3. 设置全屏被动穿透标记，避免抢占原厂触控命中区域（心跳直接由 LD_PRELOAD WidgetBase::onAnimate 驱动） */
    if (s_obj.widget_set_passive) {
        s_obj.widget_set_passive(cw, true);
    }

    s_obj.widget_set_layer(cw, &lid);
    const float xy[2] = { 0.0f, 0.0f };
    s_obj.widget_set_pos(cw, xy);
    s_obj.widget_set_size(cw, (float)ZVP_SCREEN_WIDTH, (float)ZVP_SCREEN_HEIGHT);
    s_obj.cacheimg_set_size(cw, (float)ZVP_SCREEN_WIDTH, (float)ZVP_SCREEN_HEIGHT);
    *((uint8_t *)cw + ZVP_CACHEIMG_OFF_INIT_FLAG1) = 1u;

    /* 4. 创建 640x480 RGBA4444 (fmt=7) 显存图像并绑定 Sprite */
    uint32_t param[16];
    memset(param, 0, sizeof(param));
    s_obj.create_param_ctor(param, ZVP_SCREEN_WIDTH, ZVP_SCREEN_HEIGHT, ZVP_PIXEL_FMT_RGBA4444);
    if (s_obj.create_param_arena && s_obj.widget_get_arena) {
        s_obj.create_param_arena(param, s_obj.widget_get_arena(cw));
    }

    void *img_data = (uint8_t *)cw + ZVP_CACHEIMG_OFF_IMGDATA;
    int created = s_obj.resmgr_create_img(param, img_data);
    if (!created && s_obj.create_param_arena) {
        memset(param, 0, sizeof(param));
        s_obj.create_param_ctor(param, ZVP_SCREEN_WIDTH, ZVP_SCREEN_HEIGHT, ZVP_PIXEL_FMT_RGBA4444);
        created = s_obj.resmgr_create_img(param, img_data);
    }
    if (!created) {
        zvp_logf("ResourceManager::createImage(640x480, fmt=7) 失败");
        br->canvas_failed = true;
        return -1;
    }

    void *gfx_img = s_obj.img_sys_get(img_data);
    void *bmp = gfx_img ? s_obj.gfx_get_internal(gfx_img) : NULL;
    if (!bmp) {
        zvp_logf("sys_getInternalObject 返回空 bmp");
        br->canvas_failed = true;
        return -1;
    }

    uint32_t rid = s_obj.img_get_resid(img_data);
    void *sprite = (uint8_t *)cw + ZVP_CACHEIMG_OFF_SPRITE;
    if (s_obj.sprite_set_img) {
        s_obj.sprite_set_img(sprite, rid, 0x80u);
    } else if (s_obj.cacheimg_clear) {
        /* 备用途径：clearImage 内部同样会调用 sprite_set_img */
        s_obj.cacheimg_clear(cw, 0u);
    }
    *((uint8_t *)cw + ZVP_CACHEIMG_OFF_INIT_DONE) = 1u;
    *((uint8_t *)cw + ZVP_CACHEIMG_OFF_INIT_FLAG2) = 1u;

    br->canvas_widget = cw;
    br->bmp_obj = bmp;
    br->res_id = rid;
    br->canvas_ready = true;

    zvp_bridge_clear_canvas(br);
    /* 默认隐身，心跳统一由常驻 ticker 驱动 */
    s_obj.cacheimg_set_size(cw, 0.0f, 0.0f);
    s_obj.widget_set_anim(cw, false);
    if (s_obj.sprite_invalidate) {
        s_obj.sprite_invalidate(sprite);
    }

    zvp_logf("GEN_CacheInsImg 640x480 RGBA4444 画布初始化完成: rid=0x%x bmp=%p",
             (unsigned)rid, bmp);
    return 0;
}

void zvp_bridge_set_overlay_visible(zvp_native_bridge_t *br, bool visible)
{
    if (!br || !br->canvas_ready || !br->canvas_widget || !br->layer_obj) {
        return;
    }
    if (visible) {
        s_obj.cacheimg_set_size(br->canvas_widget,
                                (float)ZVP_SCREEN_WIDTH,
                                (float)ZVP_SCREEN_HEIGHT);
        s_obj.layer_set_active(br->layer_obj, true);
        if (s_obj.layer_move_last) {
            s_obj.layer_move_last(br->layer_obj);
        }
        if (s_obj.widget_move_last) {
            s_obj.widget_move_last(br->canvas_widget);
        }
    } else {
        s_obj.cacheimg_set_size(br->canvas_widget, 0.0f, 0.0f);
        s_obj.layer_set_active(br->layer_obj, false);
    }
    if (s_obj.sprite_invalidate) {
        s_obj.sprite_invalidate((uint8_t *)br->canvas_widget + ZVP_CACHEIMG_OFF_SPRITE);
    }
}

void zvp_bridge_clear_canvas(zvp_native_bridge_t *br)
{
    if (!br || !br->canvas_ready || !br->bmp_obj) {
        return;
    }
    void *bits = NULL;
    if (s_obj.grm_lock_bits(br->bmp_obj, -1, &bits) != 0 || !bits) {
        return;
    }
    uint16_t row_px = ZVP_SCREEN_WIDTH;
    if (s_obj.grm_row_pixels) {
        uint16_t rp = 0;
        if (s_obj.grm_row_pixels(br->bmp_obj, &rp) == 0 && rp > 0) {
            row_px = rp;
        }
    }
    memset(bits, 0, (size_t)row_px * (size_t)ZVP_SCREEN_HEIGHT * 2u);
    s_obj.grm_unlock_bits(br->bmp_obj);
}

void zvp_bridge_blit_argb8888(zvp_native_bridge_t *br,
                              int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2,
                              const uint32_t *argb_pixels,
                              int32_t buf_stride_px,
                              bool rot180)
{
    if (!br || !br->canvas_ready || !br->bmp_obj || !argb_pixels) {
        return;
    }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= ZVP_SCREEN_WIDTH)  x2 = ZVP_SCREEN_WIDTH - 1;
    if (y2 >= ZVP_SCREEN_HEIGHT) y2 = ZVP_SCREEN_HEIGHT - 1;
    if (x2 < x1 || y2 < y1) {
        return;
    }

    int16_t pitch_bytes = (int16_t)(ZVP_SCREEN_WIDTH * 2);
    uintptr_t *bmp_vt = *(uintptr_t **)br->bmp_obj;
    if (bmp_vt && bmp_vt[0x20u / sizeof(uintptr_t)]) {
        typedef int (*fn_bmp_rowbytes_t)(void *bmp, int16_t *out_bytes);
        fn_bmp_rowbytes_t fn_rb = (fn_bmp_rowbytes_t)bmp_vt[0x20u / sizeof(uintptr_t)];
        int16_t rb = 0;
        if (fn_rb(br->bmp_obj, &rb) == 0 && rb > 0) {
            pitch_bytes = rb;
        }
    }

    void *bits = NULL;
    int lock_rc = s_obj.grm_lock_bits(br->bmp_obj, -1, &bits);
    static uint32_t s_blit_logs = 0;
    if (s_blit_logs < 6u) {
        s_blit_logs++;
        zvp_logf("blit #%u: rect=(%d,%d)-(%d,%d) lock_rc=%d bits=%p pitch=%d",
                 (unsigned)s_blit_logs, (int)x1, (int)y1, (int)x2, (int)y2,
                 lock_rc, bits, (int)pitch_bytes);
    }
    if (lock_rc != 0 || !bits) {
        return;
    }

    int is32 = (pitch_bytes >= (int16_t)(ZVP_SCREEN_WIDTH * 4));
    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;
    uint32_t non_zero = 0;

    for (int32_t row = 0; row < h; ++row) {
        const uint32_t *src_row = argb_pixels + row * buf_stride_px;
        int32_t dst_y = rot180 ? (ZVP_SCREEN_HEIGHT - 1 - (y1 + row)) : (y1 + row);
        uint8_t *dst_row_bytes = (uint8_t *)bits + (size_t)dst_y * (size_t)pitch_bytes;

        if (is32) {
            uint32_t *dst_row32 = (uint32_t *)dst_row_bytes;
            for (int32_t col = 0; col < w; ++col) {
                uint32_t px = src_row[col];
                if ((px >> 24) != 0u) non_zero++;
                int32_t dst_x = rot180 ? (ZVP_SCREEN_WIDTH - 1 - (x1 + col)) : (x1 + col);
                dst_row32[dst_x] = px;
            }
        } else {
            uint16_t *dst_row16 = (uint16_t *)dst_row_bytes;
            for (int32_t col = 0; col < w; ++col) {
                uint32_t px = src_row[col];
                uint32_t a = (px >> 24) & 0xFFu;
                uint32_t r = (px >> 16) & 0xFFu;
                uint32_t g = (px >> 8)  & 0xFFu;
                uint32_t b =  px        & 0xFFu;
                /* Sony 16-bit RGBA4444 (0xRGBA) */
                uint16_t rgba4444 = (uint16_t)(((r >> 4) << 12) |
                                               ((g >> 4) << 8)  |
                                               ((b >> 4) << 4)  |
                                               (a >> 4));
                if (rgba4444 != 0u) non_zero++;
                int32_t dst_x = rot180 ? (ZVP_SCREEN_WIDTH - 1 - (x1 + col)) : (x1 + col);
                dst_row16[dst_x] = rgba4444;
            }
        }
    }

    s_obj.grm_unlock_bits(br->bmp_obj);
    if (s_blit_logs <= 2u) {
        zvp_logf("blit #%u done: non_zero=%u is32=%d spr_set=%p spr_inv=%p",
                 (unsigned)s_blit_logs, (unsigned)non_zero, is32,
                 (void *)s_obj.sprite_set_img, (void *)s_obj.sprite_invalidate);
    }
}

void zvp_bridge_commit_sprite(zvp_native_bridge_t *br)
{
    if (!br || !br->canvas_ready || !br->canvas_widget) {
        return;
    }
    void *sprite = (uint8_t *)br->canvas_widget + ZVP_CACHEIMG_OFF_SPRITE;
    if (s_obj.sprite_set_img) {
        s_obj.sprite_set_img(sprite, br->res_id, 0x80u);
    }
    if (s_obj.sprite_invalidate) {
        s_obj.sprite_invalidate(sprite);
    }
}

/* ============================================================================
 * 2K cRAW 平台桥接 (/dev/mem + DmmAccess + Fsys 零拷贝直写)
 * ========================================================================== */

typedef uint32_t (*zvp_fn_fsys_mounts_t)(void);
typedef int      (*zvp_fn_fsys_find_first_t)(const char *pattern, uint32_t attr_mask, void *find_info);
typedef int      (*zvp_fn_fsys_find_next_t)(void *find_info);
typedef int      (*zvp_fn_fsys_read_t)(int handle, void *buf, uint32_t size);
typedef int      (*zvp_fn_fsys_write_t)(int hdl, const void *buf, int size);
typedef int      (*zvp_fn_fsys_seek_t)(int fh, int64_t offset, int whence, int64_t *out_pos);
typedef int      (*zvp_fn_fsys_wdirect_t)(const char *path, const void *time24,
                                          uint32_t filesize, const zvp_craw_extent_t *exts,
                                          uint32_t count);
typedef int      (*zvp_fn_fsys_get_free_t)(int drv_char, uint32_t *out_info);
typedef int      (*zvp_fn_fsys_remove_t)(const char *path);
typedef int      (*zvp_fn_mcmn_set_dir_t)(uint32_t filesize, uint32_t align_size,
                                          const zvp_craw_extent_t *exts, uint32_t count,
                                          uint32_t *out_token, uint32_t *out_aligned_size);
typedef int      (*zvp_fn_mcmn_clr_dir_t)(uint32_t token);

typedef int      (*zvp_fn_dmm_close_free_t)(uint32_t unit_handle);
typedef int      (*zvp_fn_osal_dyn_id_t)(uint32_t op, uint32_t arg1, uintptr_t arg2);
typedef int      (*zvp_fn_osal_valloc_t)(uint32_t msg_id, void **pp_msg, uint32_t size, uint32_t wait);
typedef int      (*zvp_fn_osal_snd_t)(uint32_t msg_id, void *msg);
typedef int      (*zvp_fn_osal_rcv_tmo_t)(uint32_t que_id, void **pp_msg, uint32_t tmo_ms);
typedef int      (*zvp_fn_osal_free_t)(uint32_t msg_id, void *msg);

static zvp_fn_fsys_find_next_t  s_fsys_find_next    = NULL;
static zvp_fn_fsys_read_t       s_fsys_read         = NULL;
static zvp_fn_fsys_write_t      s_craw_fsys_write   = NULL;
static zvp_fn_fsys_seek_t       s_craw_fsys_seek    = NULL;
static zvp_fn_fsys_wdirect_t    s_craw_fsys_wdirect = NULL;
static zvp_fn_fsys_get_free_t   s_craw_fsys_free    = NULL;
static zvp_fn_mcmn_set_dir_t    s_craw_mcmn_set     = NULL;
static zvp_fn_mcmn_clr_dir_t    s_craw_mcmn_clr     = NULL;

static zvp_fn_dmm_close_free_t  s_craw_dmm_free     = NULL;
static zvp_fn_osal_dyn_id_t     s_osal_dyn_id       = NULL;
static zvp_fn_osal_valloc_t     s_osal_valloc       = NULL;
static zvp_fn_osal_snd_t        s_osal_snd          = NULL;
static zvp_fn_osal_rcv_tmo_t    s_osal_rcv_tmo      = NULL;
static zvp_fn_osal_free_t       s_osal_free         = NULL;

static int craw_phys_rw(uint32_t pa, void *rw_buf, size_t len, bool is_write)
{
    if (len == 0u) {
        return 0;
    }
    if (!rw_buf) {
        return -1;
    }
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        return -2;
    }

    size_t done = 0u;
    uint8_t *p = (uint8_t *)rw_buf;
    int rc = 0;
    while (done < len) {
        uint32_t cur_pa = pa + (uint32_t)done;
        uint32_t page_pa = cur_pa & ~0xFFFu;
        uint32_t page_off = cur_pa & 0xFFFu;
        size_t chunk = 4096u - page_off;
        if (chunk > (len - done)) {
            chunk = len - done;
        }
        void *va = mmap(NULL, 4096u, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)page_pa);
        if (va == MAP_FAILED) {
            rc = -3;
            break;
        }
        volatile uint8_t *vptr = (volatile uint8_t *)va + page_off;
        if (is_write) {
            __sync_synchronize();
            if (chunk == 4u && (cur_pa & 3u) == 0u) {
                uint32_t v32 = 0u;
                memcpy(&v32, p + done, 4u);
                *(volatile uint32_t *)(uintptr_t)vptr = v32;
            } else {
                memcpy((void *)(uintptr_t)vptr, p + done, chunk);
            }
            __sync_synchronize();
        } else {
            if (chunk == 4u && (cur_pa & 3u) == 0u) {
                uint32_t v32 = *(volatile uint32_t *)(uintptr_t)vptr;
                memcpy(p + done, &v32, 4u);
            } else {
                memcpy(p + done, (const void *)(uintptr_t)vptr, chunk);
            }
        }
        munmap(va, 4096u);
        done += chunk;
    }
    close(fd);
    return rc;
}

static int craw_phys_read(uint32_t pa, void *out_buf, size_t len)
{
    return craw_phys_rw(pa, out_buf, len, false);
}

static int craw_phys_write(uint32_t pa, const void *buf, size_t len)
{
    return craw_phys_rw(pa, (void *)(uintptr_t)buf, len, true);
}

#define ZVP_DMM_OSAL_QUEUE_ID   0x0061003Cu
#define ZVP_DMM_ALLOC_TMO_MS    500u

static void *craw_resolve_osal_sym(const char *name)
{
    void *sym = dlsym(RTLD_NEXT, name);
    if (!sym) {
        sym = dlsym(RTLD_DEFAULT, name);
    }
    return sym;
}

static void craw_resolve_dmm_syms(void)
{
    if (!s_craw_dmm_free) s_craw_dmm_free = (zvp_fn_dmm_close_free_t)dlsym(RTLD_DEFAULT, "_ZN9DmmAccess15DmmCloseAndFreeEj");
    /* 必须优先使用 RTLD_NEXT 跳过本插件内 zvp_capture_probe 的同名拦截符号 */
    if (!s_osal_dyn_id)   s_osal_dyn_id   = (zvp_fn_osal_dyn_id_t)craw_resolve_osal_sym("osal_dynamic_id");
    if (!s_osal_valloc)   s_osal_valloc   = (zvp_fn_osal_valloc_t)craw_resolve_osal_sym("osal_valloc_msg_wait");
    if (!s_osal_snd)      s_osal_snd      = (zvp_fn_osal_snd_t)craw_resolve_osal_sym("osal_snd_msg");
    if (!s_osal_rcv_tmo)  s_osal_rcv_tmo  = (zvp_fn_osal_rcv_tmo_t)craw_resolve_osal_sym("osal_rcv_msg_tmo");
    if (!s_osal_free)     s_osal_free     = (zvp_fn_osal_free_t)craw_resolve_osal_sym("osal_free_msg");
}

static int craw_dmm_alloc(uint32_t unit_type, uint16_t *out_uhdl, uint32_t *out_pa, uint32_t *out_sz)
{
    craw_resolve_dmm_syms();
    if (!s_craw_dmm_free || !s_osal_dyn_id || !s_osal_valloc ||
        !s_osal_snd || !s_osal_rcv_tmo || !s_osal_free) {
        zvp_logf("[craw] ipc_dmm sym missing free=%p dyn=%p val=%p snd=%p rcv=%p fmsg=%p",
                 (void *)s_craw_dmm_free, (void *)s_osal_dyn_id, (void *)s_osal_valloc,
                 (void *)s_osal_snd, (void *)s_osal_rcv_tmo, (void *)s_osal_free);
        return -1;
    }

    uint32_t res_que = 0u;
    int qrc = s_osal_dyn_id(0x2Au, 0xC7u, (uintptr_t)&res_que);
    if (qrc != 0 || res_que == 0u) {
        zvp_logf("[craw] ipc_dmm dyn_id failed qrc=%d", qrc);
        return -2;
    }

    void *msg = NULL;
    int vrc = s_osal_valloc(ZVP_DMM_OSAL_QUEUE_ID, &msg, 24u, 1u);
    if (vrc != 0 || !msg) {
        zvp_logf("[craw] ipc_dmm valloc failed vrc=%d", vrc);
        (void)s_osal_dyn_id(0x4Au, 0u, (uintptr_t)res_que);
        return -3;
    }

    /* 对齐 libmprctrl.so (0x205fc & 0x1fdd2)：timeout=0 非阻塞 + open=1 + reply queue 在 msg-4 */
    memset(msg, 0, 24u);
    uint8_t *m = (uint8_t *)msg;
    *(uint16_t *)(m + 0)  = 1u;         /* AllocUnitNew */
    *(uint32_t *)(m + 4)  = unit_type;
    *(uint32_t *)(m + 12) = 0u;         /* timeout=0：若被占用则 dmm.ko 立即返回错误，绝不阻塞 */
    *(uint32_t *)(m - 4)  = res_que;    /* OSAL 消息头回包队列 ID */
    *(uint16_t *)(m + 20) = 0u;
    *(uint16_t *)(m + 22) = 1u;         /* open_flag=1 */

    int src = s_osal_snd(ZVP_DMM_OSAL_QUEUE_ID, msg);
    if (src != 0) {
        zvp_logf("[craw] ipc_dmm snd failed ut=0x%x src=%d", (unsigned)unit_type, src);
        (void)s_osal_dyn_id(0x4Au, 0u, (uintptr_t)res_que);
        return -4;
    }

    void *reply = NULL;
    int rrc = s_osal_rcv_tmo(res_que, &reply, ZVP_DMM_ALLOC_TMO_MS);
    if (rrc != 0 || !reply) {
        zvp_logf("[craw] ipc_dmm rcv timeout/err ut=0x%x rrc=%d", (unsigned)unit_type, rrc);
        (void)s_osal_dyn_id(0x4Au, 0u, (uintptr_t)res_que);
        return -5;
    }

    const uint8_t *rp = (const uint8_t *)reply;
    uint16_t r_id  = *(const uint16_t *)(rp + 0);
    int16_t  r_ret = *(const int16_t  *)(rp + 2);
    uint16_t uh    = *(const uint16_t *)(rp + 4);
    uint16_t ec    = *(const uint16_t *)(rp + 8);
    uint32_t pa    = (ec > 0u) ? *(const uint32_t *)(rp + 16) : 0u;
    uint32_t sz    = (ec > 0u) ? *(const uint32_t *)(rp + 20) : 0u;

    (void)s_osal_free(ZVP_DMM_OSAL_QUEUE_ID, reply);
    (void)s_osal_dyn_id(0x4Au, 0u, (uintptr_t)res_que);

    zvp_logf("[craw] ipc_dmm ut=0x%x msg=0x%x ret=%d uh=0x%x ec=%u pa=0x%08x sz=0x%x",
             (unsigned)unit_type, (unsigned)r_id, (int)r_ret,
             (unsigned)uh, (unsigned)ec, (unsigned)pa, (unsigned)sz);

    if (r_id < 0x8001u || r_id > 0x8005u || r_ret != 0 || ec == 0u || pa == 0u || sz == 0u) {
        if (uh != 0u) {
            (void)s_craw_dmm_free((uint32_t)uh);
        }
        return (r_ret != 0) ? ((int)r_ret - 10) : -6;
    }

    if (out_uhdl) *out_uhdl = uh;
    if (out_pa)   *out_pa = pa;
    if (out_sz)   *out_sz = sz;
    return 0;
}

static int craw_dmm_free(uint16_t uhdl)
{
    craw_resolve_dmm_syms();
    if (!s_craw_dmm_free) {
        return -1;
    }
    return s_craw_dmm_free((uint32_t)uhdl);
}

static void craw_resolve_fsys_syms(void)
{
    if (!s_fsys_mounts)       s_fsys_mounts       = (zvp_fn_fsys_mounts_t)dlsym(RTLD_DEFAULT, "Fsys_GetMountDrives");
    if (!s_fsys_find_first)   s_fsys_find_first   = (zvp_fn_fsys_find_first_t)dlsym(RTLD_DEFAULT, "Fsys_FindFirst");
    if (!s_fsys_find_next)    s_fsys_find_next    = (zvp_fn_fsys_find_next_t)dlsym(RTLD_DEFAULT, "Fsys_FindNext");
    if (!s_fsys_open)         s_fsys_open         = (zvp_fn_fsys_open_t)dlsym(RTLD_DEFAULT, "Fsys_Open");
    if (!s_fsys_read)         s_fsys_read         = (zvp_fn_fsys_read_t)dlsym(RTLD_DEFAULT, "Fsys_Read");
    if (!s_fsys_close)        s_fsys_close        = (zvp_fn_fsys_close_t)dlsym(RTLD_DEFAULT, "Fsys_Close");
    if (!s_craw_fsys_write)   s_craw_fsys_write   = (zvp_fn_fsys_write_t)dlsym(RTLD_DEFAULT, "Fsys_Write");
    if (!s_craw_fsys_seek)    s_craw_fsys_seek    = (zvp_fn_fsys_seek_t)dlsym(RTLD_DEFAULT, "Fsys_Seek");
    if (!s_craw_fsys_wdirect) s_craw_fsys_wdirect = (zvp_fn_fsys_wdirect_t)dlsym(RTLD_DEFAULT, "Fsys_WriteDirectFile");
    if (!s_craw_fsys_free)    s_craw_fsys_free    = (zvp_fn_fsys_get_free_t)dlsym(RTLD_DEFAULT, "Fsys_GetFreeSpace");
    if (!s_craw_mcmn_set)     s_craw_mcmn_set     = (zvp_fn_mcmn_set_dir_t)dlsym(RTLD_DEFAULT, "mcmn_buf_SetDirectFileArea");
    if (!s_craw_mcmn_clr)     s_craw_mcmn_clr     = (zvp_fn_mcmn_clr_dir_t)dlsym(RTLD_DEFAULT, "mcmn_buf_ClearDirectFileArea");
}

static int craw_fsys_seek(int fh, uint64_t offset, int whence)
{
    craw_resolve_fsys_syms();
    if (!s_craw_fsys_seek) {
        return -1;
    }
    int64_t new_pos = 0;
    return s_craw_fsys_seek(fh, (int64_t)offset, whence, &new_pos);
}

static int craw_fsys_open_direct(const char *path, int *out_fh)
{
    craw_resolve_fsys_syms();
    if (!s_fsys_open || !path || !out_fh) {
        return -1;
    }
    int fh = -1;
    int rc = s_fsys_open(path, &fh, 0x26);
    if (rc != 0 || fh < 0) {
        return rc ? rc : -2;
    }
    *out_fh = fh;
    return 0;
}

static int craw_fsys_write_direct(int fh, uint32_t align_sz,
                                  const zvp_craw_extent_t *exts, uint32_t count,
                                  uint32_t total_bytes)
{
    craw_resolve_fsys_syms();
    if (!s_craw_mcmn_set || !s_craw_mcmn_clr || !s_craw_fsys_write ||
        !exts || count == 0u || count > 8u || total_bytes == 0u) {
        return -1;
    }
    uint32_t tok = 0u;
    uint32_t aligned_sz = 0u;
    int sdc = s_craw_mcmn_set(total_bytes, align_sz, exts, count, &tok, &aligned_sz);
    if (sdc != 0 || tok == 0u) {
        return sdc ? sdc : -2;
    }
    int wr = s_craw_fsys_write(fh, (const void *)(uintptr_t)tok, (int)aligned_sz);
    (void)s_craw_mcmn_clr(tok);
    return wr;
}

static int craw_fsys_close(int fh)
{
    craw_resolve_fsys_syms();
    if (!s_fsys_close) {
        return -1;
    }
    return s_fsys_close(fh);
}

static int craw_fsys_write_file_direct(const char *path, uint32_t total_bytes,
                                       const zvp_craw_extent_t *exts, uint32_t count)
{
    craw_resolve_fsys_syms();
    if (!s_craw_fsys_wdirect || !path || !exts || count == 0u || count > 8u || total_bytes == 0u) {
        return -1;
    }
    uint8_t fsys_time[24];
    memset(fsys_time, 0, sizeof(fsys_time));
    return s_craw_fsys_wdirect(path, fsys_time, total_bytes, exts, count);
}

static int craw_fsys_get_free_mb_drv(int drv_char, uint32_t *out_free_mb)
{
    craw_resolve_fsys_syms();
    if (!s_craw_fsys_free || !out_free_mb) {
        return -1;
    }
    uint32_t info[10];
    memset(info, 0, sizeof(info));
    int rc = s_craw_fsys_free(drv_char, info);
    if (rc != 0 || info[3] == 0u || info[4] == 0u) {
        return rc ? rc : -2;
    }
    uint64_t bytes = (uint64_t)info[1] * (uint64_t)info[3] * (uint64_t)info[4];
    *out_free_mb = (uint32_t)(bytes / 1048576ull);
    return 0;
}

static int craw_fsys_get_free_mb(uint32_t *out_free_mb)
{
    return craw_fsys_get_free_mb_drv('A', out_free_mb);
}

/* 仅定点清理此前误写入相机内置小盘 B:/ 的测试残留，绝不触碰 INFO/LICENSE 原厂目录 */
void zvp_bridge_cleanup_internal_drive_b(void)
{
    zvp_fn_fsys_remove_t fn_remove =
        (zvp_fn_fsys_remove_t)dlsym(RTLD_DEFAULT, "Fsys_Remove");
    if (!fn_remove) {
        zvp_logf("[cleanup] B:/ Fsys_Remove 不可用，跳过");
        return;
    }

    static const char *const k_b_targets[] = {
        "B:/ZVC00001.RAW",
        "B:/ZVC00001.WAV",
        "B:/ZVC00002.RAW",
        "B:/ZVC00002.WAV",
        "B:/ZVPD07.BIN"
    };

    uint32_t before_mb = 0u, after_mb = 0u;
    int has_before = (craw_fsys_get_free_mb_drv('B', &before_mb) == 0);
    uint32_t removed = 0u;

    for (size_t i = 0; i < sizeof(k_b_targets) / sizeof(k_b_targets[0]); ++i) {
        if (plat_file_exists(k_b_targets[i])) {
            int rrc = fn_remove(k_b_targets[i]);
            zvp_logf("[cleanup] Fsys_Remove(%s) rc=%d", k_b_targets[i], rrc);
            if (rrc == 0) {
                removed++;
            }
        }
    }

    if (removed > 0u) {
        int has_after = (craw_fsys_get_free_mb_drv('B', &after_mb) == 0);
        zvp_logf("[cleanup] B:/ 清理完成: deleted=%u free_mb=%u->%u",
                 (unsigned)removed,
                 has_before ? (unsigned)before_mb : 0u,
                 has_after ? (unsigned)after_mb : 0u);
    } else if (has_before) {
        zvp_logf("[cleanup] B:/ 已干净 (无残留测试文件, free_mb=%u)", (unsigned)before_mb);
    }
}

static void craw_log_msg(const char *msg)
{
    if (msg) {
        zvp_logf("%s", msg);
    }
}

int zvp_bridge_init_craw_ops(zvp_craw_platform_ops_t *out_craw_ops)
{
    if (!out_craw_ops) {
        return -1;
    }
    memset(out_craw_ops, 0, sizeof(*out_craw_ops));
    out_craw_ops->phys_read              = craw_phys_read;
    out_craw_ops->phys_write             = craw_phys_write;
    out_craw_ops->dmm_alloc              = craw_dmm_alloc;
    out_craw_ops->dmm_free               = craw_dmm_free;
    out_craw_ops->fsys_open_direct       = craw_fsys_open_direct;
    out_craw_ops->fsys_write_direct      = craw_fsys_write_direct;
    out_craw_ops->fsys_close             = craw_fsys_close;
    out_craw_ops->fsys_seek              = craw_fsys_seek;
    out_craw_ops->fsys_write_file_direct = craw_fsys_write_file_direct;
    out_craw_ops->fsys_get_free_mb       = craw_fsys_get_free_mb;
    out_craw_ops->mono_ms                = zvp_monotonic_ms;
    out_craw_ops->log_msg                = craw_log_msg;
    return 0;
}

/* ============================================================================
 * Custom LUT 平台桥接 (SYS_init_module / SYS_write 0x7E5C..E / viewUnified2刷新)
 * ========================================================================== */

static int lut_plat_init_module(const void *ko_data, size_t ko_len, const char *opts)
{
#if defined(__linux__) && defined(SYS_init_module)
    if (!ko_data || ko_len == 0u) {
        return -EINVAL;
    }
    errno = 0;
    long rc = syscall(SYS_init_module, ko_data, (unsigned long)ko_len, opts ? opts : "");
    int out = (rc == 0) ? 0 : -(errno > 0 ? errno : EFAULT);
    zvp_logf("[lut] SYS_init_module(len=%u) rc=%ld out=%d",
             (unsigned)ko_len, rc, out);
    return out;
#else
    (void)ko_data;
    (void)ko_len;
    (void)opts;
    return -ENOSYS;
#endif
}

static long lut_plat_sys_write_fd(uint32_t fd, uintptr_t arg1, uintptr_t arg2)
{
#if defined(__linux__) && defined(SYS_write)
    errno = 0;
    long rc = syscall(SYS_write, (long)fd, (long)arg1, (long)arg2);
    if (rc < 0) {
        int err = errno;
        return (err > 0) ? -(long)err : rc;
    }
    return rc;
#else
    (void)fd;
    (void)arg1;
    (void)arg2;
    return -ENOSYS;
#endif
}

static int lut_plat_read_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (!path || !buf || cap == 0u) {
        return -EINVAL;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -(errno > 0 ? errno : ENOENT);
    }
    size_t total = 0u;
    int err = 0;
    while (total < cap) {
        ssize_t n = read(fd, (uint8_t *)buf + total, cap - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = -(errno > 0 ? errno : EIO);
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    if (err == 0 && total == cap) {
        uint8_t extra = 0u;
        ssize_t n;
        do {
            n = read(fd, &extra, 1u);
        } while (n < 0 && errno == EINTR);
        if (n > 0) {
            err = -EFBIG;
        }
    }
    close(fd);
    if (err == 0 && out_len) {
        *out_len = total;
    }
    return err;
}

static int lut_plat_atomic_write_file(const char *dir, const char *path, const void *buf, size_t len)
{
    if (!dir || !path || !buf || len == 0u) {
        return -EINVAL;
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        int err = -(errno > 0 ? errno : EIO);
        zvp_logf("[lut] mkdir(%s) failed rc=%d", dir, err);
        return err;
    }
    char tmp[256];
    int pn = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (pn <= 0 || (size_t)pn >= sizeof(tmp)) {
        return -ENAMETOOLONG;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -(errno > 0 ? errno : EIO);
    }
    size_t done = 0u;
    int err = 0;
    while (done < len) {
        ssize_t n = write(fd, (const uint8_t *)buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = -(errno > 0 ? errno : EIO);
            break;
        }
        if (n == 0) {
            err = -EIO;
            break;
        }
        done += (size_t)n;
    }
    if (err == 0 && fsync(fd) != 0) {
        err = -(errno > 0 ? errno : EIO);
    }
    if (close(fd) != 0 && err == 0) {
        err = -(errno > 0 ? errno : EIO);
    }
    if (err == 0 && rename(tmp, path) != 0) {
        err = -(errno > 0 ? errno : EIO);
    }
    if (err != 0) {
        (void)unlink(tmp);
        return err;
    }
    int dfd = open(dir, O_RDONLY);
    if (dfd >= 0) {
        (void)fsync(dfd);
        close(dfd);
    }
    return 0;
}

static int lut_plat_refresh_preview(int8_t req_sharpness,
                                    int8_t *io_native_sharpness,
                                    zvp_lut_preview_info_t *out_info)
{
    zvp_lut_preview_info_t local;
    memset(&local, 0, sizeof(local));

    uintptr_t vb = plat_find_module_base("viewUnified2.so");
    if (!vb) {
        local.rc = -ENODEV;
        if (out_info) *out_info = local;
        return local.rc;
    }

    static const uint8_t k_sig_get[4]    = { 0x2du, 0xe9u, 0xf0u, 0x43u };
    static const uint8_t k_sig_submit[4] = { 0x2du, 0xe9u, 0xf0u, 0x41u };
    const void *p_get    = (const void *)(vb + 0x005C03F4u);
    const void *p_submit = (const void *)(vb + 0x005C0468u);
    if (memcmp(p_get, k_sig_get, 4u) != 0 || memcmp(p_submit, k_sig_submit, 4u) != 0) {
        local.rc = -EFAULT;
        zvp_logf("[lut] viewUnified2.so 签名校验失败 vb=0x%08x", (unsigned)vb);
        if (out_info) *out_info = local;
        return local.rc;
    }

    typedef int (*fn_get_stock_t)(uintptr_t, uintptr_t,
                                  int32_t *, int32_t *, int32_t *, int32_t *, int32_t *,
                                  int32_t, int32_t);
    typedef int (*fn_submit_t)(uintptr_t, uintptr_t,
                               int32_t, int32_t, int32_t, int32_t, int32_t);

    fn_get_stock_t fn_get    = (fn_get_stock_t)(vb + 0x005C03F5u);
    fn_submit_t    fn_submit = (fn_submit_t)(vb + 0x005C0469u);

    int32_t style = 0, box = 0, contrast = 0, saturation = 0, stock_sharp = 0;
    int rc = fn_get(0u, 0u, &style, &box, &contrast, &saturation, &stock_sharp, 0, 0);
    local.style = style;
    local.box = box;
    local.contrast = contrast;
    local.saturation = saturation;
    local.stock_sharpness = stock_sharp;

    if (rc != 0 || (uint32_t)style > 19u) {
        local.rc = (rc != 0) ? rc : -EINVAL;
        zvp_logf("[lut] preview get_stock failed rc=%d style=%d", rc, (int)style);
        if (out_info) *out_info = local;
        return local.rc;
    }

    if (io_native_sharpness && zvp_lut_sharpness_valid(stock_sharp)) {
        *io_native_sharpness = (int8_t)stock_sharp;
    }

    int32_t eff_sharp = req_sharpness;
    if (eff_sharp == ZVP_LUT_SHARPNESS_NATIVE) {
        eff_sharp = stock_sharp;
    }
    if (eff_sharp < ZVP_LUT_SHARPNESS_MIN) eff_sharp = ZVP_LUT_SHARPNESS_MIN;
    if (eff_sharp > ZVP_LUT_SHARPNESS_MAX) eff_sharp = ZVP_LUT_SHARPNESS_MAX;
    local.applied_sharpness = eff_sharp;

    int src = fn_submit(0u, 0u, style, box, contrast, saturation, eff_sharp);
    local.rc = src;
    zvp_logf("[lut] preview submit rc=%d style=%d box=%d c=%d s=%d sharp=%d->%d",
             src, (int)style, (int)box, (int)contrast, (int)saturation,
             (int)stock_sharp, (int)eff_sharp);
    if (out_info) *out_info = local;
    return src;
}

static bool lut_is_fsys_drive(const char *p)
{
    return p && (p[0] >= 'A' && p[0] <= 'Z') && p[1] == ':' && p[2] == '/';
}

static bool lut_ends_with_bin_ci(const char *name)
{
    if (!name) return false;
    size_t len = strlen(name);
    if (len < 4u) return false;
    const char *ext = name + len - 4u;
    return ext[0] == '.' &&
           (ext[1] == 'b' || ext[1] == 'B') &&
           (ext[2] == 'i' || ext[2] == 'I') &&
           (ext[3] == 'n' || ext[3] == 'N');
}

static int lut_read_fsys_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    craw_resolve_fsys_syms();
    if (!s_fsys_open || !s_fsys_read || !s_fsys_close || !path || !buf || cap == 0u) {
        return -ENOSYS;
    }
    int fh = -1;
    int rc = s_fsys_open(path, &fh, 0);
    if (rc != 0 || fh < 0) {
        return rc ? rc : -ENOENT;
    }
    size_t total = 0u;
    int err = 0;
    while (total < cap) {
        size_t want = cap - total;
        if (want > 16384u) want = 16384u;
        int rd = s_fsys_read(fh, (uint8_t *)buf + total, (uint32_t)want);
        if (rd < 0) {
            err = rd;
            break;
        }
        if (rd == 0) {
            break;
        }
        total += (size_t)rd;
    }
    if (err == 0 && total == cap) {
        uint8_t extra = 0u;
        int rd = s_fsys_read(fh, &extra, 1u);
        if (rd > 0) {
            err = -EFBIG;
        }
    }
    (void)s_fsys_close(fh);
    if (err == 0 && out_len) {
        *out_len = total;
    }
    return err;
}

static int lut_plat_read_sd_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (lut_is_fsys_drive(path)) {
        return lut_read_fsys_file(path, buf, cap, out_len);
    }
    return lut_plat_read_file(path, buf, cap, out_len);
}

static void lut_try_add_sd_bundle(zvp_lut_sd_item_t *items,
                                  uint32_t max_items,
                                  uint32_t *io_count,
                                  uint8_t *scratch_buf,
                                  const char *name,
                                  const char *full_path)
{
    if (!items || !io_count || *io_count >= max_items || !scratch_buf || !name || !full_path) {
        return;
    }
    for (uint32_t i = 0u; i < *io_count; i++) {
        if (strcmp(items[i].path, full_path) == 0) {
            return;
        }
    }
    size_t got = 0u;
    int rrc = lut_plat_read_sd_file(full_path, scratch_buf, ZVP_LUT_BUNDLE_MAX_BYTES, &got);
    if (rrc != 0) {
        zvp_logf("[lut] sd read(%s) skip rc=%d", full_path, rrc);
        return;
    }
    uint32_t lcnt = 0u;
    int vrc = zvp_lut_validate_bundle(scratch_buf, got, &lcnt, NULL);
    if (vrc != 0) {
        zvp_logf("[lut] sd validate(%s len=%u) skip rc=%d", full_path, (unsigned)got, vrc);
        return;
    }
    zvp_lut_sd_item_t *it = &items[(*io_count)++];
    memset(it, 0, sizeof(*it));
    snprintf(it->name, sizeof(it->name), "%s", name);
    snprintf(it->path, sizeof(it->path), "%s", full_path);
    it->size = (uint32_t)got;
    it->look_count = (uint8_t)lcnt;
    it->is_embedded = false;
    zvp_logf("[lut] sd 发现有效包: %s (%u款, %uB)", full_path, (unsigned)lcnt, (unsigned)got);
}

static void lut_scan_fsys_dir(const char *dir_path,
                              zvp_lut_sd_item_t *items,
                              uint32_t max_items,
                              uint32_t *io_count,
                              uint8_t *scratch_buf)
{
    if (!lut_is_fsys_drive(dir_path) || !s_fsys_find_first || !s_fsys_find_next || *io_count >= max_items) {
        return;
    }
    static const char *const k_globs[] = { "*.BIN", "*.bin", "*.*" };
    uint8_t *find_info = (uint8_t *)calloc(1u, 2088u);
    if (!find_info) return;

    int rc = -1;
    for (size_t g = 0u; g < sizeof(k_globs) / sizeof(k_globs[0]); g++) {
        char pat[ZVP_LUT_SD_PATH_LEN];
        int pn = snprintf(pat, sizeof(pat), "%s/%s", dir_path, k_globs[g]);
        if (pn <= 0 || (size_t)pn >= sizeof(pat)) continue;
        memset(find_info, 0, 2088u);
        rc = s_fsys_find_first(pat, 0x80u, find_info);
        if (rc == 0) break;
    }
    if (rc == 0) {
        do {
            const char *fname = NULL;
            memcpy(&fname, find_info + 0u, sizeof(fname));
            uint8_t attr = find_info[38u];
            if (fname && fname[0] && (attr & 0x10u) == 0u && lut_ends_with_bin_ci(fname)) {
                char full[ZVP_LUT_SD_PATH_LEN];
                int fn = snprintf(full, sizeof(full), "%s/%s", dir_path, fname);
                if (fn > 0 && (size_t)fn < sizeof(full)) {
                    lut_try_add_sd_bundle(items, max_items, io_count, scratch_buf, fname, full);
                }
            }
        } while (*io_count < max_items && s_fsys_find_next(find_info) == 0);
    }
    free(find_info);
}

static void lut_scan_posix_dir(const char *dir_path,
                               zvp_lut_sd_item_t *items,
                               uint32_t max_items,
                               uint32_t *io_count,
                               uint8_t *scratch_buf)
{
    if (!dir_path || *io_count >= max_items) return;
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while (*io_count < max_items && (ent = readdir(d)) != NULL) {
        if (lut_ends_with_bin_ci(ent->d_name)) {
            char full[ZVP_LUT_SD_PATH_LEN];
            int n = snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            if (n > 0 && (size_t)n < sizeof(full)) {
                lut_try_add_sd_bundle(items, max_items, io_count, scratch_buf, ent->d_name, full);
            }
        }
    }
    closedir(d);
}

static int lut_plat_scan_sd_bundles(zvp_lut_sd_item_t *out_items, uint32_t max_items, uint32_t *out_count)
{
    if (out_count) *out_count = 0u;
    if (!out_items || max_items == 0u) return -EINVAL;

    uint8_t *scratch = (uint8_t *)malloc(ZVP_LUT_BUNDLE_MAX_BYTES);
    if (!scratch) return -ENOMEM;

    craw_resolve_fsys_syms();
    uint32_t count = 0u;

    if (s_fsys_mounts && s_fsys_find_first && s_fsys_find_next) {
        uint32_t drives = s_fsys_mounts();
        static const char *const k_sub_dirs[] = { "ZVC", "PRIVATE/SONY/PRO/LUT" };
        /* 优先扫 B:..Z:（真机 SD 卡多位于 B:），再扫 A: */
        for (uint32_t pass = 0u; pass < 2u && count < max_items; pass++) {
            uint32_t start = (pass == 0u) ? 1u : 0u;
            uint32_t end = (pass == 0u) ? 26u : 1u;
            for (uint32_t d = start; d < end && count < max_items; d++) {
                if ((drives & (1u << d)) == 0u) continue;
                for (size_t s = 0u; s < sizeof(k_sub_dirs) / sizeof(k_sub_dirs[0]); s++) {
                    char dir_buf[ZVP_LUT_SD_PATH_LEN];
                    snprintf(dir_buf, sizeof(dir_buf), "%c:/%s", (char)('A' + d), k_sub_dirs[s]);
                    lut_scan_fsys_dir(dir_buf, out_items, max_items, &count, scratch);
                }
            }
        }
    }

    static const char *const k_posix_dirs[] = { "/mnt/sd/ZVC", "/mnt/sd/PRIVATE/SONY/PRO/LUT", "/tmp/zvc_import" };
    for (size_t i = 0u; i < sizeof(k_posix_dirs) / sizeof(k_posix_dirs[0]) && count < max_items; i++) {
        lut_scan_posix_dir(k_posix_dirs[i], out_items, max_items, &count, scratch);
    }

    free(scratch);
    if (out_count) *out_count = count;
    zvp_logf("[lut] SD扫盘完成: found=%u", (unsigned)count);
    return 0;
}

int zvp_bridge_init_lut_ops(zvp_lut_platform_ops_t *out_lut_ops)
{
    if (!out_lut_ops) {
        return -1;
    }
    memset(out_lut_ops, 0, sizeof(*out_lut_ops));
    out_lut_ops->init_module         = lut_plat_init_module;
    out_lut_ops->sys_write_fd        = lut_plat_sys_write_fd;
    out_lut_ops->read_file           = lut_plat_read_file;
    out_lut_ops->atomic_write_file   = lut_plat_atomic_write_file;
    out_lut_ops->refresh_preview     = lut_plat_refresh_preview;
    out_lut_ops->scan_sd_bundles     = lut_plat_scan_sd_bundles;
    out_lut_ops->read_sd_file        = lut_plat_read_sd_file;
    out_lut_ops->default_ko_data     = g_zvp_fl4_ko_blob;
    out_lut_ops->default_ko_len      = ZVP_LUT_G_ZVP_FL4_KO_BLOB_SIZE;
    out_lut_ops->default_bundle_data = g_zvp_lut_default_bundle_blob;
    out_lut_ops->default_bundle_len  = ZVP_LUT_G_ZVP_LUT_DEFAULT_BUNDLE_BLOB_SIZE;
    return 0;
}

static int cal_plat_remove_file(const char *path)
{
    if (!path || !path[0]) {
        return -EINVAL;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return -(errno > 0 ? errno : EIO);
    }
    return 0;
}

int zvp_bridge_init_color_cal_ops(zvp_color_cal_platform_ops_t *out_ops)
{
    if (!out_ops) {
        return -1;
    }
    memset(out_ops, 0, sizeof(*out_ops));
    out_ops->phys_read         = craw_phys_read;
    out_ops->phys_write        = craw_phys_write;
    out_ops->read_file         = lut_plat_read_file;
    out_ops->atomic_write_file = lut_plat_atomic_write_file;
    out_ops->remove_file       = cal_plat_remove_file;
    return 0;
}

int zvp_bridge_init_menu_touch_ops(zvp_menu_touch_ops_t *out_ops)
{
    if (!out_ops) {
        return -1;
    }
    memset(out_ops, 0, sizeof(*out_ops));
    out_ops->find_module_base = plat_find_module_base;
    out_ops->read_mem         = plat_read_mem;
    out_ops->write_mem        = plat_write_mem;
    out_ops->should_handle_fn = zvp_runtime_should_handle_native_touch;
    return 0;
}
