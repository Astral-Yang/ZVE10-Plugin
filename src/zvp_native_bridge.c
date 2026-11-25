#define _GNU_SOURCE
#include "zvp_runtime.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* 强制将 dlsym 锁定到 GLIBC_2.4，防止新工具链引入 GLIBC_2.34 符号 */
#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

/* ============================================================================
 * 日志与基础时间辅助（严禁使用 glibc >= 2.12 高版本符号或 64-bit time_t）
 * ========================================================================== */

#define ZVP_LOG_PATH     "/tmp/zve10plugin.log"
#define ZVP_LOG_MAX_BYTES (256u * 1024u)

static uint64_t s_last_mono_ms = 0;

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
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    int probe_fd = open(ZVP_LOG_PATH, O_RDONLY);
    if (probe_fd >= 0) {
        off_t sz = lseek(probe_fd, 0, SEEK_END);
        close(probe_fd);
        if (sz >= (off_t)ZVP_LOG_MAX_BYTES) {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }
    }

    int fd = open(ZVP_LOG_PATH, flags, 0644);
    if (fd < 0) {
        return;
    }

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

    ssize_t wr = write(fd, buf, total);
    (void)wr;
    close(fd);
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

static zvp_fn_backup_read_t  s_backup_read  = NULL;
static zvp_fn_backup_write_t s_backup_write = NULL;
static zvp_fn_fsys_mounts_t  s_fsys_mounts  = NULL;
static zvp_fn_fsys_open_t    s_fsys_open    = NULL;
static zvp_fn_fsys_close_t   s_fsys_close   = NULL;

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
        return ZVP_FSYS_DRIVE_A_BIT;
    }
    return 0;
}

static int plat_file_exists(const char *path)
{
    if (!path) {
        return 0;
    }
    /* PrFILE2 路径 ("A:/...") 优先走 Sony Fsys_Open */
    if (strncmp(path, "A:/", 3) == 0) {
        if (!s_fsys_open) {
            s_fsys_open = (zvp_fn_fsys_open_t)dlsym(RTLD_DEFAULT, "Fsys_Open");
        }
        if (!s_fsys_close) {
            s_fsys_close = (zvp_fn_fsys_close_t)dlsym(RTLD_DEFAULT, "Fsys_Close");
        }
        if (s_fsys_open && s_fsys_close) {
            int handle = -1;
            int rc = s_fsys_open(path, &handle, 0);
            if (rc == 0 && handle >= 0) {
                s_fsys_close(handle);
                return 1;
            }
            return 0;
        }
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
