#define _GNU_SOURCE
#include "zvp_runtime.h"
#include "zvp_capture_probe.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

static zvp_runtime_t g_rt;

static long zvp_gettid(void)
{
#if defined(__linux__) && defined(SYS_gettid)
    return (long)syscall(SYS_gettid);
#else
    return 1L;
#endif
}

static uint32_t parse_u32_simple(const char *s)
{
    if (!s || !s[0]) return 0;
    uint32_t base = 10u;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        s += 2;
    }
    uint32_t val = 0;
    while (*s) {
        char c = *s++;
        uint32_t d = 0xFFu;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (base == 16u && c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (base == 16u && c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        val = val * base + d;
    }
    return val;
}

zvp_runtime_t *zvp_runtime_get(void)
{
    return &g_rt;
}

static inline bool should_bypass_all(void)
{
    return g_rt.boot_tripped || (g_rt.boot_evaluated && !g_rt.plugin_active);
}

bool zvp_runtime_is_bypassed(void)
{
    return should_bypass_all();
}

bool zvp_runtime_should_handle_native_touch(void)
{
    return g_rt.plugin_active && !g_rt.shutting_down && !should_bypass_all() &&
           g_rt.native_menu_touch_enabled &&
           !g_rt.input.menu_visible && !g_rt.input.mini_touch_active;
}

static int plat_bg_read_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (!path || !buf || cap == 0u) {
        return -1;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, cap);
    close(fd);
    if (n < 0) {
        return -1;
    }
    if (out_len) {
        *out_len = (size_t)n;
    }
    return 0;
}

static int plat_bg_write_file(const char *path, const void *buf, size_t len)
{
    if (!path || (!buf && len > 0u)) {
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    ssize_t wr = (len > 0u) ? write(fd, buf, len) : 0;
    (void)fsync(fd);
    close(fd);
    return (wr == (ssize_t)len) ? 0 : -1;
}

static int plat_bg_unlink_file(const char *path)
{
    return path ? unlink(path) : -1;
}

static const zvp_boot_guard_ops_t s_bg_ops = {
    plat_bg_read_file,
    plat_bg_write_file,
    plat_bg_unlink_file
};

#define ZVP_PREFS_PATH       "/tmp/zvp_prefs.bin"
#define ZVP_PREFS_MAGIC      0x5A565031u
#define ZVP_PREFS_VERSION_V1 1u
#define ZVP_PREFS_VERSION_V2 2u
#define ZVP_PREFS_VERSION    3u

typedef struct zvp_prefs_disk_v1 {
    uint32_t magic;
    uint32_t version;
    uint8_t  wifi5g_enabled;
    uint8_t  selected_channel;
    uint8_t  c1_mode;
    uint8_t  auto_flip;
    uint8_t  orig_saved;
    uint8_t  orig_nvram_go;
    uint8_t  orig_nvram_sta;
    uint8_t  orig_band_menu;
    uint8_t  orig_ap_ch_band[2];
    uint8_t  reserved[2];
} zvp_prefs_disk_v1_t;

typedef struct zvp_prefs_disk_v2 {
    uint32_t magic;
    uint32_t version;
    uint8_t  wifi5g_enabled;
    uint8_t  selected_channel;
    uint8_t  c1_mode;
    uint8_t  auto_flip;
    uint8_t  orig_saved;
    uint8_t  orig_nvram_go;
    uint8_t  orig_nvram_sta;
    uint8_t  orig_band_menu;
    uint8_t  orig_ap_ch_band[2];
    uint8_t  shutter_sound_enabled;
    uint8_t  mini_widget_enabled;
    char     sound_name[ZVP_SOUND_NAME_LEN];
} zvp_prefs_disk_v2_t;

typedef struct zvp_prefs_disk {
    uint32_t magic;
    uint32_t version;
    uint8_t  wifi5g_enabled;
    uint8_t  selected_channel;
    uint8_t  c1_mode;
    uint8_t  auto_flip;
    uint8_t  orig_saved;
    uint8_t  orig_nvram_go;
    uint8_t  orig_nvram_sta;
    uint8_t  orig_band_menu;
    uint8_t  orig_ap_ch_band[2];
    uint8_t  shutter_sound_enabled;
    uint8_t  mini_widget_enabled;
    char     sound_name[ZVP_SOUND_NAME_LEN];
    uint8_t  aspect_enabled;
    uint8_t  aspect_preset;
    uint8_t  aspect_style;
    uint8_t  aspect_guide;
    uint8_t  aspect_par_mode;
    uint8_t  aspect_reserved[3];
} zvp_prefs_disk_t;

void zvp_runtime_save_prefs(void)
{
    zvp_prefs_disk_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = ZVP_PREFS_MAGIC;
    cfg.version = ZVP_PREFS_VERSION;
    cfg.wifi5g_enabled = g_rt.wifi.enabled ? 1u : 0u;
    cfg.selected_channel = g_rt.wifi.selected_channel;
    cfg.c1_mode = (uint8_t)g_rt.input.c1_mode;
    cfg.auto_flip = g_rt.input.auto_flip ? 1u : 0u;
    cfg.orig_saved = g_rt.wifi.backup_initialized ? 1u : 0u;
    cfg.orig_nvram_go = g_rt.wifi.orig_nvram_go;
    cfg.orig_nvram_sta = g_rt.wifi.orig_nvram_sta;
    cfg.orig_band_menu = g_rt.wifi.orig_band_menu;
    cfg.orig_ap_ch_band[0] = g_rt.wifi.orig_ap_ch_band[0];
    cfg.orig_ap_ch_band[1] = g_rt.wifi.orig_ap_ch_band[1];
    cfg.shutter_sound_enabled = g_rt.shutter_sound_enabled ? 1u : 0u;
    cfg.mini_widget_enabled = g_rt.mini_widget_enabled ? 1u : 0u;
    snprintf(cfg.sound_name, sizeof(cfg.sound_name), "%s", g_rt.sound.selected_name);
    cfg.aspect_enabled = g_rt.aspect.enabled ? 1u : 0u;
    cfg.aspect_preset = g_rt.aspect.preset_idx;
    cfg.aspect_style = (uint8_t)g_rt.aspect.style;
    cfg.aspect_guide = (uint8_t)g_rt.aspect.guide;
    cfg.aspect_par_mode = (uint8_t)g_rt.aspect.par_mode;
    cfg.aspect_reserved[0] = g_rt.native_menu_touch_enabled ? 0u : 1u;

    int fd = open(ZVP_PREFS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, &cfg, sizeof(cfg));
        close(fd);
    }
}

void zvp_runtime_reset_prefs(void)
{
    (void)unlink(ZVP_PREFS_PATH);
    (void)unlink(ZVP_SOUND_ACTIVE_BIN_PATH);
}

static int zvp_runtime_full_reset_cb(void *user_data)
{
    zvp_runtime_t *rt = (zvp_runtime_t *)user_data;
    if (!rt) {
        return -1;
    }

    /* 1. 回退自定义快门音效、SoundTable 原始槽位、fopen 钩子与临时音频 */
    rt->shutter_sound_enabled = false;
    rt->mini_widget_enabled = false;
    rt->last_shutter_trig_ms = 0u;
    zvp_sound_reset(&rt->sound);

    /* 2. 回退 2K cRAW、自定义 LUT、屏幕校色、纵横比遮罩、输入按键模式与屏幕翻转自适应至插件默认状态 */
    (void)zvp_lut_shutdown_reset(&rt->lut);
    (void)zvp_color_cal_shutdown_reset(&rt->color_cal, true);
    zvp_craw_shutdown(&rt->craw);
    zvp_aspect_reset_defaults(&rt->aspect);
    zvp_input_set_c1_mode(&rt->input, ZVP_C1_MODE_SHORT_EXCLUSIVE);
    zvp_input_set_auto_flip(&rt->input, true);
    rt->native_menu_touch_enabled = false;
    zvp_menu_touch_uninstall();

    /* 3. 清除持久化偏好配置文件并同步叠加层显隐 */
    zvp_runtime_reset_prefs();
    zvp_ui_sync_overlay(&rt->ui);

    zvp_logf("全量安全复位完成: 已清除快门音/悬浮窗/纵横比/输入配置与持久化文件");
    return 0;
}

static void load_prefs_and_apply(void)
{
    int fd = open(ZVP_PREFS_PATH, O_RDONLY);
    if (fd < 0) {
        return;
    }
    zvp_prefs_disk_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ssize_t n = read(fd, &cfg, sizeof(cfg));
    close(fd);
    if (n < (ssize_t)sizeof(zvp_prefs_disk_v1_t) || cfg.magic != ZVP_PREFS_MAGIC) {
        return;
    }
    if (cfg.version != ZVP_PREFS_VERSION_V1 &&
        !(cfg.version == ZVP_PREFS_VERSION_V2 && n == (ssize_t)sizeof(zvp_prefs_disk_v2_t)) &&
        !(cfg.version == ZVP_PREFS_VERSION && n == (ssize_t)sizeof(cfg))) {
        return;
    }

    zvp_input_set_c1_mode(&g_rt.input,
                          (cfg.c1_mode == (uint8_t)ZVP_C1_MODE_LONG_PRESS)
                              ? ZVP_C1_MODE_LONG_PRESS
                              : ZVP_C1_MODE_SHORT_EXCLUSIVE);
    zvp_input_set_auto_flip(&g_rt.input, cfg.auto_flip != 0u);

    if (cfg.orig_saved) {
        if (cfg.orig_nvram_go >= 1u && cfg.orig_nvram_go <= 26u) {
            g_rt.wifi.orig_nvram_go = cfg.orig_nvram_go;
        }
        if (cfg.orig_nvram_sta >= 1u && cfg.orig_nvram_sta <= 26u) {
            g_rt.wifi.orig_nvram_sta = cfg.orig_nvram_sta;
        }
        g_rt.wifi.orig_band_menu = cfg.orig_band_menu ? 1u : 0u;
        g_rt.wifi.orig_ap_ch_band[0] = cfg.orig_ap_ch_band[0];
        g_rt.wifi.orig_ap_ch_band[1] = cfg.orig_ap_ch_band[1];
        g_rt.wifi.backup_initialized = true;
    }

    if (zvp_wifi5g_is_valid_channel(cfg.selected_channel)) {
        (void)zvp_wifi5g_select_channel(&g_rt.wifi, cfg.selected_channel);
    }
    if (cfg.wifi5g_enabled) {
        (void)zvp_wifi5g_set_enabled(&g_rt.wifi, true);
    }

    if (cfg.version >= ZVP_PREFS_VERSION_V2) {
        g_rt.shutter_sound_enabled = (cfg.shutter_sound_enabled != 0u);
        g_rt.mini_widget_enabled = (cfg.mini_widget_enabled != 0u);
        cfg.sound_name[sizeof(cfg.sound_name) - 1u] = '\0';
        if (cfg.sound_name[0]) {
            (void)zvp_sound_select_by_name(&g_rt.sound, cfg.sound_name);
        }
    }
    if (cfg.version >= ZVP_PREFS_VERSION) {
        if (cfg.aspect_preset < ZVP_ASPECT_PRESET_COUNT) {
            zvp_aspect_select_preset(&g_rt.aspect, cfg.aspect_preset);
        }
        if (cfg.aspect_style < (uint8_t)ZVP_ASPECT_STYLE_COUNT) {
            g_rt.aspect.style = (zvp_aspect_style_t)cfg.aspect_style;
        }
        if (cfg.aspect_guide < (uint8_t)ZVP_ASPECT_GUIDE_COUNT) {
            g_rt.aspect.guide = (zvp_aspect_guide_t)cfg.aspect_guide;
        }
        if (cfg.aspect_par_mode < (uint8_t)ZVP_ASPECT_PAR_COUNT) {
            g_rt.aspect.par_mode = (zvp_aspect_par_mode_t)cfg.aspect_par_mode;
        }
        zvp_aspect_set_enabled(&g_rt.aspect, cfg.aspect_enabled != 0u);
        zvp_aspect_recompute(&g_rt.aspect, zvp_input_is_ui_rotated_180(&g_rt.input));
        g_rt.native_menu_touch_enabled = (cfg.aspect_reserved[0] == 0u);
        zvp_menu_touch_set_enabled(g_rt.native_menu_touch_enabled);
    }
    if ((g_rt.shutter_sound_enabled || g_rt.mini_widget_enabled) && !g_rt.sound.prepared) {
        (void)zvp_sound_prepare_current(&g_rt.sound);
    }
}

static void rollback_state_on_gate_block(zvp_gate_status_t st,
                                         const zvp_wifi5g_platform_ops_t *wops)
{
    zvp_prefs_disk_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    int fd = open(ZVP_PREFS_PATH, O_RDONLY);
    bool has_cfg = false;
    ssize_t read_len = 0;
    if (fd >= 0) {
        read_len = read(fd, &cfg, sizeof(cfg));
        close(fd);
        if (read_len >= (ssize_t)sizeof(zvp_prefs_disk_v1_t) &&
            cfg.magic == ZVP_PREFS_MAGIC &&
            (cfg.version == ZVP_PREFS_VERSION_V1 ||
             (cfg.version == ZVP_PREFS_VERSION_V2 && read_len == (ssize_t)sizeof(zvp_prefs_disk_v2_t)) ||
             (cfg.version == ZVP_PREFS_VERSION && read_len == (ssize_t)sizeof(cfg)))) {
            has_cfg = true;
        }
    }

    int bk_rc = 0;
    if (has_cfg && (cfg.orig_saved || cfg.wifi5g_enabled)) {
        bk_rc = zvp_recovery_restore_backup_vals(
            wops, cfg.orig_saved != 0u,
            cfg.orig_nvram_go, cfg.orig_nvram_sta,
            cfg.orig_band_menu, cfg.orig_ap_ch_band);
        zvp_logf("门禁自恢复 Backup: wifi5g=%u orig_saved=%u rc=%d",
                 (unsigned)cfg.wifi5g_enabled, (unsigned)cfg.orig_saved, bk_rc);
    }

    if (st == ZVP_GATE_DISABLED_NO_CARD) {
        /* 拔卡时仅把持久化开关置回 0 防止污染，但保留原厂备份与音效选择 */
        if (has_cfg && cfg.wifi5g_enabled != 0u) {
            cfg.wifi5g_enabled = 0u;
            size_t wr_len = (cfg.version == ZVP_PREFS_VERSION_V1)
                                ? sizeof(zvp_prefs_disk_v1_t)
                                : ((cfg.version == ZVP_PREFS_VERSION_V2)
                                       ? sizeof(zvp_prefs_disk_v2_t)
                                       : sizeof(cfg));
            (void)plat_bg_write_file(ZVP_PREFS_PATH, &cfg, wr_len);
        }
    } else if (st == ZVP_GATE_DISABLED_KILL_FILE) {
        zvp_runtime_reset_prefs();
        (void)unlink(ZVP_CAL_CFG_PATH);
    } else if (st == ZVP_GATE_DISABLED_UNINSTALL) {
        zvp_runtime_reset_prefs();
        (void)unlink(ZVP_CAL_CFG_PATH);
        bool un_ok = zvp_boot_guard_disable_preload(
            &s_bg_ops, ZVP_MODE_DMODE_PATH, ZVP_MODE_PRELOAD_PATH);
        (void)unlink(ZVP_BOOT_GUARD_PATH);
        zvp_logf("ZVP_UNINSTALL 一键卸载: ok=%d (已还原 dmode=3 与 preload 空)", un_ok ? 1 : 0);
    }
}

/* 仅在 UI 主线程执行非阻塞开机卡控门禁评估，严禁 usleep 卡死系统 */
static void ensure_boot_evaluated_ui_thread(uint64_t now)
{
    if (g_rt.boot_evaluated) {
        return;
    }
    if (g_rt.boot_start_ms == 0u) {
        g_rt.boot_start_ms = now;
    }

    zvp_wifi5g_platform_ops_t wops;
    zvp_gate_ops_t gops;
    zvp_bridge_init_platform(&wops, &gops);

    static uint64_t s_last_gate_poll_ms = 0u;
    if (s_last_gate_poll_ms != 0u && (now - s_last_gate_poll_ms) < 200u &&
        (now - g_rt.boot_start_ms) < 20000u) {
        return;
    }
    s_last_gate_poll_ms = now;

    zvp_gate_status_t st = zvp_gate_evaluate_boot(&gops, 0u, 0u);
    uint32_t cur_drives = gops.get_mount_drives ? gops.get_mount_drives() : 0u;
    if (st == ZVP_GATE_DISABLED_NO_CARD && (now - g_rt.boot_start_ms) < 20000u) {
        /* 实测 ZV-E10 Fsys 在开机约 11.5s 挂载 B: SD 卡，最多非阻塞等待 20s */
        return;
    }

    g_rt.boot_evaluated = true;
    g_rt.gate_status = st;

    if (st != ZVP_GATE_ALLOW) {
        g_rt.plugin_active = false;
        zvp_bridge_stop_ticker(&g_rt.bridge);
        rollback_state_on_gate_block(st, &wops);
        if (st != ZVP_GATE_DISABLED_UNINSTALL) {
            zvp_boot_guard_mark_healthy(&s_bg_ops, ZVP_BOOT_GUARD_PATH);
        }
        g_rt.guard_cleared = true;
        zvp_logf("卡控门禁拦截启动: %s drives=0x%x elapsed=%llums (保持纯原厂运行)",
                 zvp_gate_status_str(st), (unsigned)cur_drives,
                 (unsigned long long)(now - g_rt.boot_start_ms));
        return;
    }
    zvp_logf("卡控门禁放行: %s drives=0x%x elapsed=%llums",
             zvp_gate_status_str(st), (unsigned)cur_drives,
             (unsigned long long)(now - g_rt.boot_start_ms));

    /* 允许通过环境变量 ZVP_C1_KEY 覆盖默认 C1 键码以便探针/调试 */
    uint32_t c1_code = ZVP_KEY_C1_DEFAULT;
    const char *env_c1 = getenv("ZVP_C1_KEY");
    if (env_c1 && env_c1[0]) {
        uint32_t parsed = parse_u32_simple(env_c1);
        if (parsed != 0u) {
            c1_code = parsed;
        }
    }

    zvp_craw_platform_ops_t cops;
    zvp_bridge_init_craw_ops(&cops);
    zvp_craw_init(&g_rt.craw, &cops);
    zvp_bridge_cleanup_internal_drive_b();

    zvp_wifi5g_init(&g_rt.wifi, &wops);
    zvp_recovery_init(&g_rt.rec);
    zvp_recovery_set_full_reset_cb(&g_rt.rec, zvp_runtime_full_reset_cb, &g_rt);
    zvp_sound_init(&g_rt.sound);
    zvp_aspect_init(&g_rt.aspect);

    zvp_lut_platform_ops_t lops;
    zvp_bridge_init_lut_ops(&lops);
    zvp_lut_init(&g_rt.lut, &lops);
    int lut_boot_rc = zvp_lut_boot_activate(&g_rt.lut);
    g_rt.lut_last_nonzero_sel = (g_rt.lut.active_sel > 0u) ? g_rt.lut.active_sel : 1u;
    zvp_logf("[lut] boot activate rc=%d slots=%u active_sel=%u kmod=%d preview=%d",
             lut_boot_rc, (unsigned)g_rt.lut.num_slots,
             (unsigned)g_rt.lut.active_sel,
             g_rt.lut.kmod_loaded ? 1 : 0,
             g_rt.lut.last_preview.rc);

    zvp_color_cal_platform_ops_t ccops;
    zvp_bridge_init_color_cal_ops(&ccops);
    zvp_color_cal_init(&g_rt.color_cal, &ccops);
    int cal_boot_rc = zvp_color_cal_boot_activate(&g_rt.color_cal);
    zvp_logf("[cal] boot activate rc=%d cap=%d en=%d preset=%u",
             cal_boot_rc,
             g_rt.color_cal.factory_captured ? 1 : 0,
             g_rt.color_cal.enabled ? 1 : 0,
             (unsigned)g_rt.color_cal.preset_idx);

    zvp_input_init(&g_rt.input, c1_code);
    g_rt.native_menu_touch_enabled = true;
    load_prefs_and_apply();

    if (g_rt.native_menu_touch_enabled) {
        zvp_menu_touch_ops_t mops;
        if (zvp_bridge_init_menu_touch_ops(&mops) == 0) {
            (void)zvp_menu_touch_init(&mops);
        }
    }
    zvp_menu_touch_set_enabled(g_rt.native_menu_touch_enabled);

    zvp_bridge_ensure_canvas(&g_rt.bridge);

    __sync_synchronize();
    g_rt.active_since_ms = now;
    g_rt.plugin_active = true;
    if (g_rt.mini_widget_enabled || g_rt.aspect.enabled) {
        zvp_ui_sync_overlay(&g_rt.ui);
    }
    zvp_logf("zve10plugin UI主线程激活成功: tid=%ld c1_key=0x%x canvas=%d sounds=%u",
             g_rt.ui_tid, (unsigned)c1_code, g_rt.bridge.canvas_ready ? 1 : 0,
             (unsigned)g_rt.sound.count);
}

#include <unistd.h>

__attribute__((constructor))
static void zvp_on_preload_init(void)
{
    memset(&g_rt, 0, sizeof(g_rt));
    g_rt.boot_start_ms = zvp_monotonic_ms();

    /* 仅允许注入相机主进程 im.elf，避免在 /etc/ld.so.preload 或子进程 fork/exec 场景下误触发门禁/看门狗 */
    char exe_path[256];
    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1u);
    if (exe_len <= 0) {
        g_rt.boot_tripped = true;
        g_rt.boot_evaluated = true;
        g_rt.guard_cleared = true;
        g_rt.plugin_active = false;
        return;
    }
    exe_path[(size_t)exe_len] = '\0';
    if (!zvp_is_target_im_exe_path(exe_path)) {
        g_rt.boot_tripped = true;
        g_rt.boot_evaluated = true;
        g_rt.guard_cleared = true;
        g_rt.plugin_active = false;
        return;
    }

    zvp_boot_guard_state_t bg;
    if (zvp_boot_guard_enter(&s_bg_ops,
                             ZVP_BOOT_GUARD_PATH,
                             ZVP_MODE_DMODE_PATH,
                             ZVP_MODE_PRELOAD_PATH,
                             ZVP_BOOT_GUARD_MAX_FAILS,
                             &bg)) {
        g_rt.boot_tripped = true;
        g_rt.boot_evaluated = true;
        g_rt.guard_cleared = true;
        g_rt.plugin_active = false;
        zvp_logf("开机看门狗自熔断触发: 连续异常>=%u次 (累计熔断=%u), 已自动还原mode3并清空preload",
                 (unsigned)ZVP_BOOT_GUARD_MAX_FAILS, (unsigned)bg.trip_count);
    }
}

__attribute__((destructor))
static void zvp_on_process_exit(void)
{
    g_rt.shutting_down = true;
    (void)zvp_color_cal_shutdown_reset(&g_rt.color_cal, false);
    zvp_craw_shutdown(&g_rt.craw);
    if (!g_rt.boot_tripped && g_rt.boot_evaluated && !g_rt.guard_cleared) {
        zvp_boot_guard_mark_healthy(&s_bg_ops, ZVP_BOOT_GUARD_PATH);
        g_rt.guard_cleared = true;
    }
}

void zvp_runtime_handle_action_mask(uint32_t act_mask)
{
    if (!g_rt.plugin_active) {
        return;
    }
    if (act_mask & ZVP_ACT_TOGGLE_MENU) {
        zvp_logf("menu_toggle -> visible=%d (canvas_ready=%d)",
                 g_rt.input.menu_visible ? 1 : 0,
                 g_rt.bridge.canvas_ready ? 1 : 0);
        if (!g_rt.input.menu_visible) {
            (void)zvp_lut_flush_pending_cfg(&g_rt.lut);
            (void)zvp_color_cal_flush_cfg(&g_rt.color_cal);
            g_rt.lvgl.cur_touch_pressed = false;
            g_rt.tp_head = g_rt.tp_tail;
        }
        zvp_ui_set_visible(&g_rt.ui, g_rt.input.menu_visible);
    } else if (act_mask & ZVP_ACT_CLOSE_MENU) {
        zvp_logf("menu_close -> visible=0");
        (void)zvp_lut_flush_pending_cfg(&g_rt.lut);
        (void)zvp_color_cal_flush_cfg(&g_rt.color_cal);
        g_rt.lvgl.cur_touch_pressed = false;
        g_rt.tp_head = g_rt.tp_tail;
        zvp_ui_set_visible(&g_rt.ui, false);
    }

    if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_NAV_DOWN |
                    ZVP_ACT_NAV_LEFT | ZVP_ACT_NAV_RIGHT | ZVP_ACT_NAV_ENTER |
                    ZVP_ACT_DIAL_NEXT | ZVP_ACT_DIAL_PREV)) {
        if (!zvp_ui_handle_nav_key(&g_rt.ui, act_mask)) {
            if (act_mask & ZVP_ACT_NAV_UP)    zvp_lvgl_port_push_key(&g_rt.lvgl, LV_KEY_PREV);
            if (act_mask & ZVP_ACT_NAV_DOWN)  zvp_lvgl_port_push_key(&g_rt.lvgl, LV_KEY_NEXT);
            if (act_mask & ZVP_ACT_NAV_LEFT)  zvp_lvgl_port_push_key(&g_rt.lvgl, LV_KEY_LEFT);
            if (act_mask & ZVP_ACT_NAV_RIGHT) zvp_lvgl_port_push_key(&g_rt.lvgl, LV_KEY_RIGHT);
            if (act_mask & ZVP_ACT_NAV_ENTER) zvp_lvgl_port_push_key(&g_rt.lvgl, LV_KEY_ENTER);
        }
    }
}

void zvp_runtime_on_animate(void)
{
    if (!g_rt.plugin_active) {
        return;
    }

    uint64_t now = zvp_monotonic_ms();

    /* 排空静态拍照管线只读测绘探针日志 */
    zvp_capture_probe_drain_logs();

    /* 0. 在 UI 主线程安全打印菜单开启期按键诊断码（限前 256 条，屏蔽陀螺仪/姿态传感器刷屏） */
    static uint32_t s_kl_logs = 0;
    while (g_rt.kl_head != g_rt.kl_tail) {
        uint32_t h = g_rt.kl_head;
        zvp_keylog_sample_t ks = g_rt.keylog_queue[h];
        __sync_synchronize();
        g_rt.kl_head = (h + 1u) % ZVP_KEYLOG_QUEUE_SIZE;
        if ((ks.w0 & 0xFFFF8000u) == 0x15078000u) {
            continue;
        }
        g_rt.diag_last_key_w0 = ks.w0;
        bool is_std_rel = (ks.w2 == 0u) &&
            (ks.w0 == ZVP_KEY_UP || ks.w0 == ZVP_KEY_DOWN ||
             ks.w0 == ZVP_KEY_LEFT || ks.w0 == ZVP_KEY_RIGHT ||
             ks.w0 == ZVP_KEY_MENU || ks.w0 == ZVP_KEY_C1_DEFAULT ||
             ks.w0 == ZVP_KEY_C1_DEFOCUS_AUX);
        if (s_kl_logs < 256u && !is_std_rel) {
            s_kl_logs++;
            zvp_logf("menu_key #%u: w0=0x%08x w2=0x%08x consumed=%d",
                     (unsigned)s_kl_logs, (unsigned)ks.w0, (unsigned)ks.w2, ks.consumed ? 1 : 0);
        }
    }

    /* 1. 排空输入线程 SPSC 按键动作队列 */
    bool had_key_nav = false;
    while (g_rt.q_head != g_rt.q_tail) {
        uint32_t h = g_rt.q_head;
        uint32_t act = g_rt.act_queue[h];
        __sync_synchronize();
        g_rt.q_head = (h + 1u) % ZVP_ACT_QUEUE_SIZE;

        if (act & (ZVP_ACT_NAV_UP | ZVP_ACT_NAV_DOWN | ZVP_ACT_NAV_LEFT |
                   ZVP_ACT_NAV_RIGHT | ZVP_ACT_NAV_ENTER |
                   ZVP_ACT_DIAL_NEXT | ZVP_ACT_DIAL_PREV)) {
            had_key_nav = true;
        }
        zvp_runtime_handle_action_mask(act);
    }

    /* 2. 使用事件驱动模式 (LV_INDEV_MODE_EVENT) 逐条直读触控样本，彻底杜绝 33ms 定时器丢包 */
    bool drained_touch = false;
    static uint32_t s_tp_ui_logs = 0;
    static bool s_prev_tp_log_down = false;
    while (g_rt.tp_head != g_rt.tp_tail) {
        uint32_t h = g_rt.tp_head;
        zvp_touch_sample_t smp = g_rt.touch_queue[h];
        __sync_synchronize();
        g_rt.tp_head = (h + 1u) % ZVP_TOUCH_QUEUE_SIZE;

        g_rt.diag_last_tp_x = smp.x;
        g_rt.diag_last_tp_y = smp.y;
        if (s_tp_ui_logs < 120u && (smp.pressed != s_prev_tp_log_down)) {
            s_tp_ui_logs++;
            zvp_logf("tp ui #%u: raw=(%d,%d) ui=(%d,%d) down=%d",
                     (unsigned)s_tp_ui_logs, (int)smp.raw_x, (int)smp.raw_y,
                     (int)smp.x, (int)smp.y, smp.pressed ? 1 : 0);
        }
        s_prev_tp_log_down = smp.pressed;

        g_rt.lvgl.cur_touch_x = smp.x;
        g_rt.lvgl.cur_touch_y = smp.y;
        g_rt.lvgl.cur_touch_pressed = smp.pressed;
        if ((g_rt.input.menu_visible || g_rt.input.mini_widget_visible) &&
            g_rt.lvgl.initialized && g_rt.lvgl.indev_touch) {
            lv_tick_inc(15u);
            lv_indev_read(g_rt.lvgl.indev_touch);
            drained_touch = true;
        }
    }
    g_rt.touch_dirty = false;

    bool flip_pending = false;
    if (g_rt.flip_dirty) {
        g_rt.flip_dirty = false;
        flip_pending = true;
    }

    bool aspect_need_sync = false;
    if (g_rt.aspect_hw_dirty) {
        g_rt.aspect_hw_dirty = false;
        if (g_rt.pending_vdf_valid) {
            int16_t vx = g_rt.pending_vdf_x;
            int16_t vy = g_rt.pending_vdf_y;
            int16_t vw = g_rt.pending_vdf_w;
            int16_t vh = g_rt.pending_vdf_h;
            if (!g_rt.aspect.hw.vdf_rect_valid ||
                g_rt.aspect.hw.vdf_rect.x != vx || g_rt.aspect.hw.vdf_rect.y != vy ||
                g_rt.aspect.hw.vdf_rect.w != vw || g_rt.aspect.hw.vdf_rect.h != vh) {
                zvp_aspect_update_vdf_rect(&g_rt.aspect, vx, vy, vw, vh);
                aspect_need_sync = true;
            }
        }
        if (g_rt.pending_panel_valid) {
            uint8_t r0 = g_rt.pending_panel_raw[0];
            uint8_t r1 = g_rt.pending_panel_raw[1];
            bool p16 = (r1 == 2u);
            if (!g_rt.aspect.hw.panel_aspect_valid ||
                g_rt.aspect.hw.vdf_panel_raw[0] != r0 ||
                g_rt.aspect.hw.vdf_panel_raw[1] != r1) {
                zvp_aspect_update_panel_aspect(&g_rt.aspect, p16, r0, r1);
                aspect_need_sync = true;
            }
        }
    }

    static uint64_t s_last_view_poll_ms = 0u;
    if (g_rt.aspect.enabled &&
        (s_last_view_poll_ms == 0u || (now - s_last_view_poll_ms) >= 80u)) {
        s_last_view_poll_ms = now;
        extern void zvp_aspect_poll_native_view_state(bool *out_changed);
        bool view_changed = false;
        zvp_aspect_poll_native_view_state(&view_changed);
        if (view_changed) {
            aspect_need_sync = true;
        }
    }

    if (flip_pending) {
        aspect_need_sync = true;
    }

    if (aspect_need_sync) {
        zvp_aspect_recompute(&g_rt.aspect, zvp_input_is_ui_rotated_180(&g_rt.input));
        static uint32_t s_asp_sync_logs = 0u;
        if (s_asp_sync_logs < 32u) {
            s_asp_sync_logs++;
            uint32_t p_push = 0u, p_drain = 0u, p_dl = 0u, p_dh = 0u, p_df = 0u;
            int p_lk = 0;
            zvp_capture_probe_get_stats(&p_push, &p_drain, &p_dl, &p_dh, &p_df, &p_lk);
            zvp_logf("aspect sync #%u: en=%d view=(%d,%d,%d,%d) frame=(%d,%d,%d,%d) menu=%d pb=%d probe=(%u/%u dl=%u dh=%u df=%u lk=%d)",
                     (unsigned)s_asp_sync_logs, g_rt.aspect.enabled ? 1 : 0,
                     (int)g_rt.aspect.layout.view_rect.x, (int)g_rt.aspect.layout.view_rect.y,
                     (int)g_rt.aspect.layout.view_rect.w, (int)g_rt.aspect.layout.view_rect.h,
                     (int)g_rt.aspect.layout.frame_rect.x, (int)g_rt.aspect.layout.frame_rect.y,
                     (int)g_rt.aspect.layout.frame_rect.w, (int)g_rt.aspect.layout.frame_rect.h,
                     g_rt.aspect.hw.camera_in_menu ? 1 : 0,
                     g_rt.aspect.hw.camera_in_playback ? 1 : 0,
                     (unsigned)p_push, (unsigned)p_drain,
                     (unsigned)p_dl, (unsigned)p_dh, (unsigned)p_df, p_lk);
        }
        if (g_rt.aspect.enabled || g_rt.ui.built) {
            zvp_ui_sync_overlay(&g_rt.ui);
        }
    }

    /* 3. 处理长按阈值定时器与后台业务轮询 */
    if (!g_rt.guard_cleared && g_rt.active_since_ms != 0u &&
        now >= g_rt.active_since_ms &&
        (now - g_rt.active_since_ms) >= ZVP_BOOT_GUARD_HEALTHY_MS) {
        zvp_boot_guard_mark_healthy(&s_bg_ops, ZVP_BOOT_GUARD_PATH);
        g_rt.guard_cleared = true;
        zvp_logf("开机看门狗已解除: 插件已稳定运行 %llums",
                 (unsigned long long)(now - g_rt.active_since_ms));
    }
    /* 诊断期暂停 12s 自动开启 OSAL 抓包刷屏，保留 /tmp/zve10plugin.log 关键轨迹 */
    (void)now;

    uint32_t tick_act = zvp_input_tick(&g_rt.input, now);
    if (tick_act != ZVP_ACT_NONE) {
        zvp_runtime_handle_action_mask(tick_act);
    }

    zvp_wifi5g_state_t prev_st = g_rt.wifi.state;
    zvp_wifi5g_poll(&g_rt.wifi);
    if (g_rt.wifi.state != prev_st && g_rt.ui.built) {
        zvp_ui_refresh(&g_rt.ui);
    }

    zvp_recovery_phase_t prev_rec = g_rt.rec.phase;
    zvp_recovery_tick(&g_rt.rec, now);
    if (g_rt.rec.phase != prev_rec && g_rt.ui.built) {
        zvp_ui_refresh(&g_rt.ui);
    }

    /* LUT 滚到即预览防抖延迟落盘 (600ms 无新切换时写入 zvc.cfg，避免连续拨轮卡顿) */
    if (g_rt.lut.cfg_dirty && !g_rt.lut.fine_editing &&
        g_rt.lut_cfg_dirty_ms != 0u && now >= g_rt.lut_cfg_dirty_ms &&
        (now - g_rt.lut_cfg_dirty_ms) >= 600u) {
        g_rt.lut_cfg_dirty_ms = 0u;
        (void)zvp_lut_flush_pending_cfg(&g_rt.lut);
    }

    uint32_t prev_cal_reapply = g_rt.color_cal.reapply_count;
    zvp_color_cal_tick(&g_rt.color_cal, now);
    if (g_rt.color_cal.reapply_count != prev_cal_reapply) {
        zvp_logf("[cal] 检测到固件重置 Slot0，已自动重注并刷新 (#%u)",
                 (unsigned)g_rt.color_cal.reapply_count);
    }

    if (g_rt.craw.liro_installed &&
        (g_rt.ui.active_page == ZVP_PAGE_2K_CRAW || g_rt.craw.mode != ZVP_CRAW_LIRO_BYPASS)) {
        uint32_t prev_rdc = g_rt.craw.shm_snap.rdc_calls;
        uint32_t prev_dfe = g_rt.craw.shm_snap.dfe_rdc_calls + g_rt.craw.shm_snap.dfe_main_calls;
        uint32_t prev_aif = g_rt.craw.shm_snap.aif_calls;
        uint32_t prev_wr  = g_rt.craw.shm_snap.write_seq;
        if (zvp_craw_poll_telemetry(&g_rt.craw, now) == 0 && g_rt.ui.built &&
            g_rt.ui.active_page == ZVP_PAGE_2K_CRAW &&
            (g_rt.craw.shm_snap.rdc_calls != prev_rdc ||
             (g_rt.craw.shm_snap.dfe_rdc_calls + g_rt.craw.shm_snap.dfe_main_calls) != prev_dfe ||
             g_rt.craw.shm_snap.aif_calls != prev_aif ||
             g_rt.craw.shm_snap.write_seq != prev_wr)) {
            zvp_ui_refresh(&g_rt.ui);
        }
    }

    if (flip_pending && g_rt.ui.built) {
        zvp_ui_refresh(&g_rt.ui);
        if ((g_rt.input.menu_visible || g_rt.input.mini_widget_visible ||
             zvp_aspect_should_display(&g_rt.aspect, g_rt.input.menu_visible)) &&
            g_rt.ui.scr) {
            lv_obj_invalidate(g_rt.ui.scr);
        }
    }

    /* 4. 仅在 UI 主线程执行 LVGL 刷新与 Sprite 提交（菜单 / 悬浮窗 / 纵横比遮罩） */
    if ((g_rt.input.menu_visible || g_rt.input.mini_widget_visible ||
         zvp_aspect_should_display(&g_rt.aspect, g_rt.input.menu_visible)) &&
        g_rt.lvgl.initialized) {
        if (had_key_nav || drained_touch) {
            lv_tick_inc(15u);
        }
        zvp_lvgl_port_tick_and_render(&g_rt.lvgl);
    }
}

void zvp_runtime_ui_frame_tick(void)
{
    if (g_rt.shutting_down || should_bypass_all()) {
        return;
    }

    long tid = zvp_gettid();
    if (g_rt.ui_tid == 0) {
        g_rt.ui_tid = tid;
    } else if (tid != g_rt.ui_tid) {
        return;
    }

    static volatile int s_in_anim = 0;
    if (s_in_anim != 0) {
        return;
    }
    s_in_anim = 1;

    uint64_t now = zvp_monotonic_ms();
    ensure_boot_evaluated_ui_thread(now);

    if (g_rt.plugin_active) {
        if (!g_rt.bridge.canvas_ready && !g_rt.bridge.canvas_failed) {
            zvp_bridge_ensure_canvas(&g_rt.bridge);
        }

        bool has_pending = (g_rt.q_head != g_rt.q_tail) ||
                           (g_rt.tp_head != g_rt.tp_tail) ||
                           g_rt.touch_dirty || g_rt.flip_dirty;
        if (has_pending || g_rt.last_anim_ms == 0u || (now - g_rt.last_anim_ms) >= 16u) {
            g_rt.last_anim_ms = now;
            g_rt.bridge.anim_ticks++;
            zvp_runtime_on_animate();
        }
    }

    s_in_anim = 0;
}

static void ui_thread_bootstrap_if_needed(void)
{
    if (g_rt.shutting_down || should_bypass_all()) {
        return;
    }
    if (g_rt.bridge.ticker_ready || g_rt.bridge.ticker_failed) {
        return;
    }

    static volatile int s_in_boot = 0;
    if (!__sync_bool_compare_and_swap(&s_in_boot, 0, 1)) {
        return;
    }

    if (!g_rt.bridge.ticker_ready && !g_rt.bridge.ticker_failed) {
        zvp_bridge_ensure_ticker(&g_rt.bridge);
    }

    __sync_synchronize();
    s_in_boot = 0;
}

/* ============================================================================
 * Sony UI 主线程哨兵钩子 + 输入/createEvent 总闸阻断钩子
 * ========================================================================== */

typedef void  (*fn_widget_set_mode_t)(void *self, int mode);
typedef void  (*fn_set_disp_state_t)(void *self, bool disp);
typedef int   (*fn_snd_event_t)(zvp_infra_input_event_t *ev);
typedef int   (*fn_cb_panel_t)(int dev, zvp_infra_input_event_t *ev);
typedef int   (*fn_cb_tp_t)(int dev, zvp_infra_input_event_t *ev);
typedef void *(*fn_create_event_t)(void *self, unsigned long ev_id, unsigned char flag);
typedef void  (*fn_set_flip_t)(void *self, bool flip, bool b2);
typedef void  (*fn_set_r180_t)(void *self, bool r180);

void _ZN2ux6wgtsys6Widget7setModeENS0_10WidgetModeE(void *self, int mode)
{
    static fn_widget_set_mode_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_widget_set_mode_t)dlsym(RTLD_NEXT, "_ZN2ux6wgtsys6Widget7setModeENS0_10WidgetModeE");
    }
    if (real_fn) {
        real_fn(self, mode);
    }
    ui_thread_bootstrap_if_needed();
}

void _ZN2ux6wgtsys6Widget7setModeENS1_10WidgetModeE(void *self, int mode)
{
    static fn_widget_set_mode_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_widget_set_mode_t)dlsym(RTLD_NEXT, "_ZN2ux6wgtsys6Widget7setModeENS1_10WidgetModeE");
    }
    if (real_fn) {
        real_fn(self, mode);
    }
    ui_thread_bootstrap_if_needed();
}

void _ZN20LayoutableWidgetBase12setDispStateEb(void *self, bool disp)
{
    static fn_set_disp_state_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_set_disp_state_t)dlsym(RTLD_NEXT, "_ZN20LayoutableWidgetBase12setDispStateEb");
    }
    if (real_fn) {
        real_fn(self, disp);
    }
    ui_thread_bootstrap_if_needed();
}

static bool is_touch_event(const zvp_infra_input_event_t *ev)
{
    if (!ev) return false;
    return (ev->w0 == ZVP_EV_W0_TOUCH_MAGIC) || ((ev->w1 & 0xFFFFu) == ZVP_EV_TYPE_TOUCH);
}

static void enqueue_action_mask(uint32_t act)
{
    uint32_t q_act = act & ~(ZVP_ACT_CONSUME_SYS | ZVP_ACT_DEFER_C1 | ZVP_ACT_REPLAY_C1);
    if (q_act == ZVP_ACT_NONE) {
        return;
    }
    uint32_t tail = g_rt.q_tail;
    uint32_t next = (tail + 1u) % ZVP_ACT_QUEUE_SIZE;
    if (next != g_rt.q_head) {
        g_rt.act_queue[tail] = q_act;
        __sync_synchronize();
        g_rt.q_tail = next;
    }
}

static void enqueue_touch_sample(int16_t raw_x, int16_t raw_y, int16_t x, int16_t y, bool pressed)
{
    uint32_t tail = g_rt.tp_tail;
    uint32_t next = (tail + 1u) % ZVP_TOUCH_QUEUE_SIZE;
    if (next == g_rt.tp_head) {
        return;
    }
    g_rt.touch_queue[tail].raw_x = raw_x;
    g_rt.touch_queue[tail].raw_y = raw_y;
    g_rt.touch_queue[tail].x = x;
    g_rt.touch_queue[tail].y = y;
    g_rt.touch_queue[tail].pressed = pressed;
    __sync_synchronize();
    g_rt.tp_tail = next;
}

static void enqueue_keylog(uint32_t w0, uint32_t w2, bool consumed)
{
    uint32_t tail = g_rt.kl_tail;
    uint32_t next = (tail + 1u) % ZVP_KEYLOG_QUEUE_SIZE;
    if (next == g_rt.kl_head) {
        return;
    }
    g_rt.keylog_queue[tail].w0 = w0;
    g_rt.keylog_queue[tail].w2 = w2;
    g_rt.keylog_queue[tail].consumed = consumed;
    __sync_synchronize();
    g_rt.kl_tail = next;
}

#define ZVP_SUPPRESS_SLOTS 8u
typedef struct zvp_suppress_slot {
    volatile long tid;
    volatile uint64_t until_ms;
} zvp_suppress_slot_t;

static zvp_suppress_slot_t s_suppress_slots[ZVP_SUPPRESS_SLOTS];

static void mark_thread_suppress(long tid, bool enable)
{
    uint64_t deadline = enable ? (zvp_monotonic_ms() + 15u) : 0u;
    int empty_idx = -1;
    for (int i = 0; i < (int)ZVP_SUPPRESS_SLOTS; ++i) {
        if (s_suppress_slots[i].tid == tid) {
            s_suppress_slots[i].until_ms = deadline;
            __sync_synchronize();
            return;
        }
        if (empty_idx < 0 && s_suppress_slots[i].tid == 0L) {
            empty_idx = i;
        }
    }
    if (enable && empty_idx >= 0) {
        if (__sync_bool_compare_and_swap(&s_suppress_slots[empty_idx].tid, 0L, tid)) {
            s_suppress_slots[empty_idx].until_ms = deadline;
            __sync_synchronize();
        }
    }
}

static bool should_suppress_thread(long tid)
{
    uint64_t now = zvp_monotonic_ms();
    for (int i = 0; i < (int)ZVP_SUPPRESS_SLOTS; ++i) {
        if (s_suppress_slots[i].tid == tid) {
            uint64_t until = s_suppress_slots[i].until_ms;
            return (until != 0u && now <= until);
        }
    }
    return false;
}

static bool is_dial_event_id(unsigned long ev_id)
{
    uint32_t c = (uint32_t)ev_id;
    return (c >= 0x15070030u && c <= 0x1507004Fu) ||
           (c >= 0x150700D0u && c <= 0x150700DFu) ||
           (c >= 0x150701C0u && c <= 0x150701CFu);
}

static bool is_shutter_event_id(unsigned long ev_id)
{
    uint32_t c = (uint32_t)ev_id;
    return (c >= 0x15010020u && c <= 0x1501003Fu) ||
           (c >= 0x15010230u && c <= 0x1501029Fu);
}

static bool is_touch_event_id(unsigned long ev_id)
{
    uint32_t c = (uint32_t)ev_id;
    return (c == 0x11013001u || c == 0x11013008u ||
            c == 0x17005004u || c == 0x17005005u);
}

static bool is_sensor_noise_event_id(uint32_t ev_id)
{
    /* 0x150780c0 / 0x15078300 等为电子水平仪/机身姿态传感器事件，非用户按键 */
    return (ev_id & 0xFFFF8000u) == 0x15078000u;
}

static volatile int s_shutter_trig_lock = 0;

static void trigger_shutter_sound_if_enabled(uint64_t now)
{
    if (!g_rt.plugin_active || !g_rt.shutter_sound_enabled) {
        return;
    }
    if (!__sync_bool_compare_and_swap(&s_shutter_trig_lock, 0, 1)) {
        return;
    }
    bool fire = (g_rt.last_shutter_trig_ms == 0u) ||
                (now >= g_rt.last_shutter_trig_ms && (now - g_rt.last_shutter_trig_ms) >= 90u);
    if (fire) {
        g_rt.last_shutter_trig_ms = now;
    }
    __sync_synchronize();
    s_shutter_trig_lock = 0;

    if (fire) {
        /* 输入线程快路径只允许零阻塞播音（确保已在 UI 主线程提前 prepare） */
        (void)zvp_sound_play_prepared(&g_rt.sound, now);
    }
}

/* 输入线程快路径：<10us，严禁磁盘 I/O、usleep、LVGL 或 libObj 调用 */
static uint32_t dispatch_event_fast(const zvp_infra_input_event_t *ev)
{
    if (!g_rt.plugin_active || !ev) {
        return ZVP_ACT_NONE;
    }

    if (is_touch_event(ev)) {
        /* libObj.so 0x11cb80 (callback_tp_raw) / 0x11e6ae 与实机坐标确认：
         * _InfraInputEvent 偏移 +4 (w1 低16位) = Y (0..479)，偏移 +6 (w1 高16位) = X (0..639) */
        int16_t raw_x = ZVP_TOUCH_UNPACK_X(ev->w1);
        int16_t raw_y = ZVP_TOUCH_UNPACK_Y(ev->w1);
        bool is_down = (ev->w2 != 0u);
        uint32_t tact = zvp_input_process_touch(&g_rt.input, raw_x, raw_y, is_down);
        if (tact & ZVP_ACT_CONSUME_SYS) {
            enqueue_touch_sample(raw_x, raw_y, g_rt.input.touch_x, g_rt.input.touch_y, g_rt.input.touch_pressed);
            g_rt.touch_dirty = true;
            __sync_synchronize();
        }
        return (tact & ZVP_ACT_CONSUME_SYS) ? ZVP_ACT_CONSUME_SYS : ZVP_ACT_NONE;
    }

    uint32_t keycode = ev->w0;
    uint64_t now = zvp_monotonic_ms();
    /* S2 快门全按按下 (0x15010030)：不拦截，仅异步触发自定义快门音 */
    if (keycode == 0x15010030u && ev->w2 != 0u) {
        trigger_shutter_sound_if_enabled(now);
    }
    bool was_menu = g_rt.input.menu_visible;
    uint32_t kact = ZVP_ACT_NONE;
    if (zvp_input_is_dial_keycode(keycode)) {
        kact = zvp_input_process_dial(&g_rt.input, keycode, (int32_t)ev->w2);
    } else {
        bool is_press = (ev->w2 != 0u);
        kact = zvp_input_process_key(&g_rt.input, keycode, is_press, now);
    }
    bool consumed = (kact & ZVP_ACT_CONSUME_SYS) != 0u;
    if (kact != ZVP_ACT_NONE) {
        enqueue_action_mask(kact);
    }
    bool is_c1_key = (keycode == g_rt.input.c1_keycode || keycode == ZVP_KEY_C1_DEFOCUS_AUX);
    if ((was_menu || g_rt.input.menu_visible || is_c1_key) && keycode != 0u &&
        !is_sensor_noise_event_id(keycode)) {
        enqueue_keylog(keycode, ev->w2, consumed);
    }
    return kact;
}

typedef struct zvp_deferred_c1_pkt {
    bool is_snd;
    void *fn;
    int dev;
    zvp_infra_input_event_t ev;
} zvp_deferred_c1_pkt_t;

#define ZVP_C1_DEFER_MAX 4u
static zvp_deferred_c1_pkt_t s_c1_defer_pkts[ZVP_C1_DEFER_MAX];
static uint32_t s_c1_defer_cnt = 0u;
static volatile long s_key_dispatch_tid = 0;

static void record_deferred_c1_pkt(bool is_snd, void *fn, int dev, const zvp_infra_input_event_t *ev)
{
    if (!ev || !fn) {
        return;
    }
    if (ev->w0 == g_rt.input.c1_keycode) {
        s_c1_defer_cnt = 0u;
    }
    for (uint32_t i = 0u; i < s_c1_defer_cnt; i++) {
        if (s_c1_defer_pkts[i].is_snd == is_snd &&
            s_c1_defer_pkts[i].ev.w0 == ev->w0) {
            return;
        }
    }
    if (s_c1_defer_cnt < ZVP_C1_DEFER_MAX) {
        s_c1_defer_pkts[s_c1_defer_cnt].is_snd = is_snd;
        s_c1_defer_pkts[s_c1_defer_cnt].fn = fn;
        s_c1_defer_pkts[s_c1_defer_cnt].dev = dev;
        s_c1_defer_pkts[s_c1_defer_cnt].ev = *ev;
        s_c1_defer_cnt++;
    }
}

static void replay_deferred_c1_pkts(void)
{
    uint32_t cnt = s_c1_defer_cnt;
    s_c1_defer_cnt = 0u;
    for (uint32_t i = 0u; i < cnt; i++) {
        zvp_deferred_c1_pkt_t *p = &s_c1_defer_pkts[i];
        if (!p->fn) {
            continue;
        }
        if (p->is_snd) {
            ((fn_snd_event_t)p->fn)(&p->ev);
        } else {
            ((fn_cb_panel_t)p->fn)(p->dev, &p->ev);
        }
    }
}

static int dispatch_key_entry_impl(bool is_snd, void *fn_ptr, int dev, zvp_infra_input_event_t *ev)
{
    long tid = zvp_gettid();
    if (__sync_bool_compare_and_swap(&s_key_dispatch_tid, 0L, tid)) {
        uint32_t kact = dispatch_event_fast(ev);
        bool consumed = (kact & ZVP_ACT_CONSUME_SYS) != 0u;
        if (kact & ZVP_ACT_DEFER_C1) {
            record_deferred_c1_pkt(is_snd, fn_ptr, dev, ev);
        }
        mark_thread_suppress(tid, consumed);
        if (consumed) {
            __sync_bool_compare_and_swap(&s_key_dispatch_tid, tid, 0L);
            return 0;
        }
        if (kact & ZVP_ACT_REPLAY_C1) {
            replay_deferred_c1_pkts();
        }
        int ret = 0;
        if (fn_ptr) {
            ret = is_snd ? ((fn_snd_event_t)fn_ptr)(ev)
                         : ((fn_cb_panel_t)fn_ptr)(dev, ev);
        }
        __sync_bool_compare_and_swap(&s_key_dispatch_tid, tid, 0L);
        return ret;
    }
    if (!fn_ptr) {
        return 0;
    }
    return is_snd ? ((fn_snd_event_t)fn_ptr)(ev)
                  : ((fn_cb_panel_t)fn_ptr)(dev, ev);
}

int snd_InfraInputEvent_to_InputServant(zvp_infra_input_event_t *ev)
{
    static fn_snd_event_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_snd_event_t)dlsym(RTLD_NEXT, "snd_InfraInputEvent_to_InputServant");
    }
    if (should_bypass_all()) {
        return real_fn ? real_fn(ev) : 0;
    }
    return dispatch_key_entry_impl(true, (void *)real_fn, 0, ev);
}

static int dispatch_key_cb_common(fn_cb_panel_t *real_cache, const char *sym, int dev, zvp_infra_input_event_t *ev)
{
    if (!*real_cache) {
        *real_cache = (fn_cb_panel_t)dlsym(RTLD_NEXT, sym);
    }
    if (should_bypass_all()) {
        return *real_cache ? (*real_cache)(dev, ev) : 0;
    }
    return dispatch_key_entry_impl(false, (void *)(*real_cache), dev, ev);
}

int _ZN12InputService14callback_panelEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService14callback_panelEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService15callback_directEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService15callback_directEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService16callback_convertEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService16callback_convertEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService53callback_direct_not_user_operation_and_not_reload_mssEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService53callback_direct_not_user_operation_and_not_reload_mssEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService20callback_convert_capEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService20callback_convert_capEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService26callback_convert_focus_magEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService26callback_convert_focus_magEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService26callback_dial_signed_speedEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService26callback_dial_signed_speedEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService18callback_mode_dialEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_panel_t real_fn = NULL;
    return dispatch_key_cb_common(&real_fn, "_ZN12InputService18callback_mode_dialEiP16_InfraInputEvent", dev, ev);
}

static volatile long s_tp_dispatch_tid = 0;

static int dispatch_tp_cb_common(fn_cb_tp_t *real_cache, const char *sym, int dev, zvp_infra_input_event_t *ev)
{
    if (!*real_cache) {
        *real_cache = (fn_cb_tp_t)dlsym(RTLD_NEXT, sym);
    }
    if (should_bypass_all()) {
        return *real_cache ? (*real_cache)(dev, ev) : 0;
    }
    long tid = zvp_gettid();
    if (__sync_bool_compare_and_swap(&s_tp_dispatch_tid, 0L, tid)) {
        bool consumed = dispatch_event_fast(ev);
        mark_thread_suppress(tid, consumed);
        if (consumed) {
            __sync_bool_compare_and_swap(&s_tp_dispatch_tid, tid, 0L);
            return 0;
        }
        int ret = *real_cache ? (*real_cache)(dev, ev) : 0;
        __sync_bool_compare_and_swap(&s_tp_dispatch_tid, tid, 0L);
        return ret;
    }
    return *real_cache ? (*real_cache)(dev, ev) : 0;
}

int _ZN12InputService11callback_tpEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_tp_t real_fn = NULL;
    return dispatch_tp_cb_common(&real_fn, "_ZN12InputService11callback_tpEiP16_InfraInputEvent", dev, ev);
}

int _ZN12InputService15callback_tp_rawEiP16_InfraInputEvent(int dev, zvp_infra_input_event_t *ev)
{
    static fn_cb_tp_t real_fn = NULL;
    return dispatch_tp_cb_common(&real_fn, "_ZN12InputService15callback_tp_rawEiP16_InfraInputEvent", dev, ev);
}

typedef void (*fn_post_mouse_btn_t)(void *, unsigned char, unsigned char, const void *);
typedef void (*fn_post_mouse_move_t)(void *, unsigned char, short, short, const void *);

void _ZN2ux6wgtsys12WidgetSystem14postMousePressEhhNS_4core8MsecTimeE(void *self, unsigned char id, unsigned char btn, const void *msec)
{
    static fn_post_mouse_btn_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_post_mouse_btn_t)dlsym(RTLD_NEXT, "_ZN2ux6wgtsys12WidgetSystem14postMousePressEhhNS_4core8MsecTimeE");
    }
    if (g_rt.plugin_active && (g_rt.input.menu_visible || g_rt.input.mini_touch_active)) {
        return;
    }
    if (real_fn) {
        real_fn(self, id, btn, msec);
    }
}

void _ZN2ux6wgtsys12WidgetSystem13postMouseMoveEhssNS_4core8MsecTimeE(void *self, unsigned char id, short x, short y, const void *msec)
{
    static fn_post_mouse_move_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_post_mouse_move_t)dlsym(RTLD_NEXT, "_ZN2ux6wgtsys12WidgetSystem13postMouseMoveEhssNS_4core8MsecTimeE");
    }
    if (g_rt.plugin_active && (g_rt.input.menu_visible || g_rt.input.mini_touch_active)) {
        return;
    }
    if (real_fn) {
        real_fn(self, id, x, y, msec);
    }
}

void _ZN2ux6wgtsys12WidgetSystem16postMouseReleaseEhhNS_4core8MsecTimeE(void *self, unsigned char id, unsigned char btn, const void *msec)
{
    static fn_post_mouse_btn_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_post_mouse_btn_t)dlsym(RTLD_NEXT, "_ZN2ux6wgtsys12WidgetSystem16postMouseReleaseEhhNS_4core8MsecTimeE");
    }
    if (g_rt.plugin_active &&
        (g_rt.input.menu_visible || g_rt.input.mini_touch_active || g_rt.input.mini_suppress_release)) {
        g_rt.input.mini_suppress_release = false;
        return;
    }
    if (real_fn) {
        real_fn(self, id, btn, msec);
    }
}

/* 总闸：libObj.so 内 callback_panel / callback_direct / callback_convert / callback_tp 全部走此 PLT 分配 Event。
 * 当事件已被插件消费时直接返回 NULL，Sony 调用方安全跳过 pushFilterEvent / pushEvent，零泄露零透传。 */
void *_ZN12InputService11createEventEmh(void *self, unsigned long ev_id, unsigned char flag)
{
    static fn_create_event_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_create_event_t)dlsym(RTLD_NEXT, "_ZN12InputService11createEventEmh");
    }
    if (should_bypass_all()) {
        return real_fn ? real_fn(self, ev_id, flag) : NULL;
    }
    long tid = zvp_gettid();
    if (should_suppress_thread(tid)) {
        return NULL;
    }
    if (g_rt.plugin_active) {
        uint32_t ev32_s = (uint32_t)ev_id;
        if ((ev32_s & 0xFFFFFFF0u) == 0x15010030u && (ev32_s == 0x15010030u || (ev32_s & 0xFu) == 2u)) {
            trigger_shutter_sound_if_enabled(zvp_monotonic_ms());
        }
    }
    if (g_rt.plugin_active && g_rt.input.menu_visible) {
        if (is_touch_event_id(ev_id)) {
            return NULL;
        }
        uint32_t ev32 = (uint32_t)ev_id;
        /* 实机日志确认：ZV-E10 轮盘中心 OK 键走 viewUnified2.so 0x429b14 直接调用 createEvent，
         * 不经过 snd_InfraInputEvent_to_InputServant：按下=0x15010042，松开=0x15010041，连发=0x15010044 */
        if (ev32 == ZVP_EV_ENTER_PRESS || ev32 == ZVP_EV_ENTER_RELEASE) {
            bool is_press = (ev32 == ZVP_EV_ENTER_PRESS);
            uint32_t kact = zvp_input_process_key(&g_rt.input, ZVP_KEY_ENTER, is_press, zvp_monotonic_ms());
            if (kact != ZVP_ACT_NONE) {
                enqueue_action_mask(kact);
            }
        }
        uint32_t hi = ev32 & 0xFFFF0000u;
        bool is_input_group = (hi == 0x15010000u || hi == 0x15020000u ||
                               hi == 0x15030000u || hi == 0x15040000u ||
                               hi == 0x15050000u || hi == 0x15070000u);
        if (is_input_group && !is_shutter_event_id(ev_id) &&
            !is_sensor_noise_event_id((uint32_t)ev_id)) {
            enqueue_keylog((uint32_t)ev_id, 0xCE000000u | (uint32_t)flag, true);
        }
        if (((hi == 0x15010000u || hi == 0x15020000u || hi == 0x15030000u) &&
             !is_shutter_event_id(ev_id)) ||
            is_dial_event_id(ev_id)) {
            return NULL;
        }
    }
    return real_fn ? real_fn(self, ev_id, flag) : NULL;
}

void _ZN12InputService13setFlipStatusEbb(void *self, bool flip, bool b2)
{
    static fn_set_flip_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_set_flip_t)dlsym(RTLD_NEXT, "_ZN12InputService13setFlipStatusEbb");
    }
    if (should_bypass_all()) {
        if (real_fn) real_fn(self, flip, b2);
        return;
    }
    bool prev_rot = zvp_input_is_ui_rotated_180(&g_rt.input);
    zvp_input_update_flip(&g_rt.input, flip);
    if (real_fn) {
        real_fn(self, flip, b2);
    }
    if (prev_rot != zvp_input_is_ui_rotated_180(&g_rt.input)) {
        g_rt.flip_dirty = true;
        __sync_synchronize();
    }
}

void _ZN12InputService16setViewCoordR180Eb(void *self, bool r180)
{
    static fn_set_r180_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (fn_set_r180_t)dlsym(RTLD_NEXT, "_ZN12InputService16setViewCoordR180Eb");
    }
    if (should_bypass_all()) {
        if (real_fn) real_fn(self, r180);
        return;
    }
    bool prev_rot = zvp_input_is_ui_rotated_180(&g_rt.input);
    zvp_input_update_flip(&g_rt.input, r180);
    if (real_fn) {
        real_fn(self, r180);
    }
    if (prev_rot != zvp_input_is_ui_rotated_180(&g_rt.input)) {
        g_rt.flip_dirty = true;
        __sync_synchronize();
    }
}

/* ============================================================================
 * 音频系统拦截与动态重映射钩子 (ObjSystemSound / GlobalAccessor / fopen)
 * ========================================================================== */

typedef void     (*fn_ga_set_cat_t)(void *self, unsigned int cat_id);
typedef void     (*fn_ga_reg_sw_t)(void *self, unsigned int idx, void *ptr);
typedef int      (*fn_issue_async_t)(void *obj_if, void *msg);
typedef uint32_t (*fn_msg_get_cat_t)(const void *msg);

static void *volatile s_sound_ga = NULL;

void _ZN14GlobalAccessor16SetObjCategoryIDEj(void *self, unsigned int cat_id)
{
    static fn_ga_set_cat_t real_fn = NULL;
    fn_ga_set_cat_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_ga_set_cat_t)dlsym(RTLD_NEXT, "_ZN14GlobalAccessor16SetObjCategoryIDEj");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    if (!should_bypass_all() && cat_id == 0x3410u) {
        __atomic_store_n(&s_sound_ga, self, __ATOMIC_RELEASE);
    }
    if (fn) {
        fn(self, cat_id);
    }
}

void _ZN14GlobalAccessor13RegistSwBlockEjPv(void *self, unsigned int idx, void *ptr)
{
    static fn_ga_reg_sw_t real_fn = NULL;
    fn_ga_reg_sw_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_ga_reg_sw_t)dlsym(RTLD_NEXT, "_ZN14GlobalAccessor13RegistSwBlockEjPv");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    if (!should_bypass_all()) {
        void *ga = __atomic_load_n(&s_sound_ga, __ATOMIC_ACQUIRE);
        if (self && self == ga && idx == 2u && ptr) {
            zvp_sound_on_sys_data(ptr);
        }
    }
    if (fn) {
        fn(self, idx, ptr);
    }
}

int _ZN3MWF5ObjIf17IssueCommandAsyncEPvPNS_6ObjMsgE(void *obj_if, void *msg)
{
    static fn_issue_async_t real_fn = NULL;
    static fn_msg_get_cat_t get_cat_fn = NULL;
    fn_issue_async_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_issue_async_t)dlsym(RTLD_NEXT, "_ZN3MWF5ObjIf17IssueCommandAsyncEPvPNS_6ObjMsgE");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    if (!should_bypass_all() && obj_if && msg) {
        fn_msg_get_cat_t cat_fn = __atomic_load_n(&get_cat_fn, __ATOMIC_ACQUIRE);
        if (!cat_fn) {
            cat_fn = (fn_msg_get_cat_t)dlsym(RTLD_DEFAULT, "_ZNK3MWF6ObjMsg13GetCategoryIdEv");
            if (cat_fn) {
                __atomic_store_n(&get_cat_fn, cat_fn, __ATOMIC_RELEASE);
            }
        }
        if (cat_fn && cat_fn(msg) == 0x3410u) {
            zvp_sound_on_obj_if(obj_if);
        }
    }
    return fn ? fn(obj_if, msg) : -1;
}

/* ============================================================================
 * 纵横比取景视图状态监测与 VdfCh 布局被动观察钩子（绝不篡改原厂参数）
 * ========================================================================== */

typedef int  (*fn_vdf_set_yuv_t)(void *self, const void *prm);
typedef int  (*fn_vdf_get_yuv_t)(void *self, const void *req, void *comp);
typedef int  (*fn_vdf_get_panel_aspect_t)(void *self, void *comp);
typedef bool (*fn_view_bool_getter_t)(void);

void zvp_aspect_poll_native_view_state(bool *out_changed)
{
    static int s_resolved = 0;
    static fn_view_bool_getter_t fn_menu = NULL;
    static fn_view_bool_getter_t fn_pb = NULL;
    static fn_view_bool_getter_t fn_pb_open = NULL;
    static fn_view_bool_getter_t fn_panel_16_9 = NULL;

    if (!s_resolved) {
        s_resolved = 1;
        fn_menu = (fn_view_bool_getter_t)dlsym(RTLD_DEFAULT, "_ZN27CmnForUseMultipleViewStatus22getMenuExistenceStatusEv");
        fn_pb = (fn_view_bool_getter_t)dlsym(RTLD_DEFAULT, "_ZN27CmnForUseMultipleViewStatus20getPbExistenceStatusEv");
        fn_pb_open = (fn_view_bool_getter_t)dlsym(RTLD_DEFAULT, "_ZN18WrapperSettingUtil14isPlaybackOpenEv");
        fn_panel_16_9 = (fn_view_bool_getter_t)dlsym(RTLD_DEFAULT, "_ZN21CmnViewFeatureManager17isPanelAspect16_9Ev");
        zvp_logf("视图状态符号解析: menu=%p pb=%p pb_open=%p p16_9=%p",
                 (void *)fn_menu, (void *)fn_pb, (void *)fn_pb_open, (void *)fn_panel_16_9);
    }

    bool in_menu = fn_menu ? fn_menu() : false;
    bool in_pb = (fn_pb && fn_pb()) || (fn_pb_open && fn_pb_open());

    bool changed = false;
    if (g_rt.aspect.hw.camera_in_menu != in_menu ||
        g_rt.aspect.hw.camera_in_playback != in_pb) {
        zvp_aspect_update_camera_ui_state(&g_rt.aspect, in_menu, in_pb);
        changed = true;
    }

    if (fn_panel_16_9) {
        bool p16 = fn_panel_16_9();
        if (!g_rt.aspect.hw.panel_aspect_valid ||
            g_rt.aspect.hw.panel_is_16_9 != p16) {
            zvp_aspect_update_panel_aspect(&g_rt.aspect, p16,
                                           g_rt.aspect.hw.vdf_panel_raw[0],
                                           g_rt.aspect.hw.vdf_panel_raw[1]);
            changed = true;
        }
    }

    if (out_changed) {
        *out_changed = changed;
    }
}

static void observe_vdf_yuv_rect(void *vdf_self, const void *prm_buf, const char *src_tag)
{
    if (!vdf_self || !prm_buf || should_bypass_all()) {
        return;
    }
    uint16_t ch = *(const uint16_t *)((const uint8_t *)vdf_self + 0x24u);
    const uint8_t *p = (const uint8_t *)prm_buf;
    int16_t dst_x = *(const int16_t *)(p + 20u);
    int16_t dst_y = *(const int16_t *)(p + 22u);
    int16_t dst_w = *(const int16_t *)(p + 24u);
    int16_t dst_h = *(const int16_t *)(p + 26u);

    static uint32_t s_vdf_logs = 0u;
    if (s_vdf_logs < 32u) {
        s_vdf_logs++;
        zvp_logf("VdfCh(%u) %s: dst=(%d,%d,%d,%d) route=%u",
                 (unsigned)ch, src_tag ? src_tag : "?",
                 (int)dst_x, (int)dst_y, (int)dst_w, (int)dst_h, (unsigned)p[8]);
    }

    /* 主 LCD LiveView 通道为 41 (0x29)；仅在收到合理屏幕矩形时更新观测缓存 */
    if (ch == 41u &&
        dst_w >= 160 && dst_h >= 120 &&
        dst_x >= 0 && dst_y >= 0 &&
        (int32_t)dst_x + (int32_t)dst_w <= ZVP_SCREEN_WIDTH &&
        (int32_t)dst_y + (int32_t)dst_h <= ZVP_SCREEN_HEIGHT) {
        g_rt.pending_vdf_x = dst_x;
        g_rt.pending_vdf_y = dst_y;
        g_rt.pending_vdf_w = dst_w;
        g_rt.pending_vdf_h = dst_h;
        g_rt.pending_vdf_valid = true;
        __sync_synchronize();
        g_rt.aspect_hw_dirty = true;
    }
}

int _ZN17OBJAVBBCBASECLASS5VdfCh12setYUVLayoutERKN3VDF28VDF_PRM_INPUT_SET_YUV_LAYOUTE(void *self, const void *prm)
{
    static fn_vdf_set_yuv_t real_fn = NULL;
    fn_vdf_set_yuv_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_vdf_set_yuv_t)dlsym(RTLD_NEXT, "_ZN17OBJAVBBCBASECLASS5VdfCh12setYUVLayoutERKN3VDF28VDF_PRM_INPUT_SET_YUV_LAYOUTE");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    int rc = fn ? fn(self, prm) : -1;
    if (rc == 0) {
        observe_vdf_yuv_rect(self, prm, "setYUV");
    }
    return rc;
}

int _ZN17OBJAVBBCBASECLASS5VdfCh12getYuvLayoutERKN3VDF28VDF_PRM_INPUT_GET_YUV_LAYOUTERNS1_33VDF_PRM_INPUT_GET_YUV_LAYOUT_COMPE(void *self, const void *req, void *comp)
{
    static fn_vdf_get_yuv_t real_fn = NULL;
    fn_vdf_get_yuv_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_vdf_get_yuv_t)dlsym(RTLD_NEXT, "_ZN17OBJAVBBCBASECLASS5VdfCh12getYuvLayoutERKN3VDF28VDF_PRM_INPUT_GET_YUV_LAYOUTERNS1_33VDF_PRM_INPUT_GET_YUV_LAYOUT_COMPE");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    int rc = fn ? fn(self, req, comp) : -1;
    if (rc == 0) {
        observe_vdf_yuv_rect(self, comp, "getYUV");
    }
    return rc;
}

int _ZN17OBJAVBBCBASECLASS5VdfCh14getPanelAspectERN3VDF34VDF_PRM_DISP_GET_PANEL_ASPECT_COMPE(void *self, void *comp)
{
    static fn_vdf_get_panel_aspect_t real_fn = NULL;
    fn_vdf_get_panel_aspect_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (fn_vdf_get_panel_aspect_t)dlsym(RTLD_NEXT, "_ZN17OBJAVBBCBASECLASS5VdfCh14getPanelAspectERN3VDF34VDF_PRM_DISP_GET_PANEL_ASPECT_COMPE");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    int rc = fn ? fn(self, comp) : -1;
    if (rc == 0 && self && comp && !should_bypass_all()) {
        uint16_t ch = *(const uint16_t *)((const uint8_t *)self + 0x24u);
        const uint8_t *c = (const uint8_t *)comp;
        static uint32_t s_pa_logs = 0u;
        if (s_pa_logs < 8u) {
            s_pa_logs++;
            zvp_logf("VdfCh(%u) getPanelAspect: raw=[%u, %u]",
                     (unsigned)ch, (unsigned)c[8], (unsigned)c[9]);
        }
        if (ch == 41u) {
            g_rt.pending_panel_raw[0] = c[8];
            g_rt.pending_panel_raw[1] = c[9];
            g_rt.pending_panel_valid = true;
            __sync_synchronize();
            g_rt.aspect_hw_dirty = true;
        }
    }
    return rc;
}
