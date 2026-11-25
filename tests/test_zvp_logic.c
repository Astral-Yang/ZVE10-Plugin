#include "zvp_core.h"
#include "zvp_abi.h"
#include "zvp_sound.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Mock 硬件环境（模拟模块映射、只读/可写内存与 Sony Backup）
 * ========================================================================== */

static uint32_t g_mock_mounts = 0;
static bool g_mock_zvp_off_exists = false;
static uint32_t g_mock_sleep_calls = 0;

static uint8_t g_mock_netsvc_mem[64];
static uint8_t g_mock_caution_mem[64];
static bool g_mock_netsvc_loaded = false;
static bool g_mock_caution_loaded = false;

static uint8_t g_mock_bk_5g_enable = 0;
static uint8_t g_mock_bk_ap_pair[2] = { ZVP_WIFI_BAND_24G, 6 };
static uint8_t g_mock_bk_nvram_go = 0x09u;
static uint8_t g_mock_bk_nvram_sta = 0x09u;
static uint8_t g_mock_bk_band_menu = 0x00u;
static int g_mock_backup_write_count = 0;
static int g_mock_bk_5g_enable_write_count = 0;

static const char *g_mock_kill_path = NULL;

static uint32_t mock_get_mounts(void) { return g_mock_mounts; }
static int mock_file_exists(const char *path)
{
    if (!path) return 0;
    if (!g_mock_zvp_off_exists) {
        return 0;
    }
    const char *target = g_mock_kill_path ? g_mock_kill_path : ZVP_GATE_DISABLE_PATH_FSYS;
    if (strcmp(path, target) == 0) {
        return 1;
    }
    return 0;
}
static void mock_sleep(uint32_t ms) { (void)ms; g_mock_sleep_calls++; }

static uintptr_t mock_find_base(const char *name)
{
    if (strcmp(name, "libNetService.so") == 0 && g_mock_netsvc_loaded) {
        return (uintptr_t)0x10000000u;
    }
    if (strcmp(name, "CautionConfig.so") == 0 && g_mock_caution_loaded) {
        return (uintptr_t)0x20000000u;
    }
    return 0;
}

static int mock_read_mem(uintptr_t addr, void *out, size_t len)
{
    uintptr_t net_target = 0x10000000u + ZVP_OFF_NETSVC_ISVALID_5GHZ;
    uintptr_t cau_target = 0x20000000u + ZVP_OFF_CAUTION_WIFI_CASE;
    if (addr == net_target && len <= sizeof(g_mock_netsvc_mem)) {
        memcpy(out, g_mock_netsvc_mem, len);
        return 0;
    }
    if (addr == cau_target && len <= sizeof(g_mock_caution_mem)) {
        memcpy(out, g_mock_caution_mem, len);
        return 0;
    }
    return -1;
}

static int mock_write_mem(uintptr_t addr, const void *buf, size_t len)
{
    uintptr_t net_target = 0x10000000u + ZVP_OFF_NETSVC_ISVALID_5GHZ;
    uintptr_t cau_target = 0x20000000u + ZVP_OFF_CAUTION_WIFI_CASE;
    if (addr == net_target && len <= sizeof(g_mock_netsvc_mem)) {
        memcpy(g_mock_netsvc_mem, buf, len);
        return 0;
    }
    if (addr == cau_target && len <= sizeof(g_mock_caution_mem)) {
        memcpy(g_mock_caution_mem, buf, len);
        return 0;
    }
    return -1;
}

static int mock_backup_read(uint32_t bkid, void *buf)
{
    if (bkid == ZVP_BKID_WIFI_5G_ENABLE) {
        *(uint8_t *)buf = g_mock_bk_5g_enable;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_AP_CH_BAND) {
        memcpy(buf, g_mock_bk_ap_pair, 2);
        return 2;
    }
    if (bkid == ZVP_BKID_WIFI_NVRAM_GO) {
        *(uint8_t *)buf = g_mock_bk_nvram_go;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_NVRAM_STA) {
        *(uint8_t *)buf = g_mock_bk_nvram_sta;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_BAND_MENU) {
        *(uint8_t *)buf = g_mock_bk_band_menu;
        return 1;
    }
    return -1;
}

static int mock_backup_write(uint32_t subsys, uint32_t bkid, const void *buf)
{
    assert(subsys == (bkid >> 16));
    g_mock_backup_write_count++;
    if (bkid == ZVP_BKID_WIFI_5G_ENABLE) {
        g_mock_bk_5g_enable_write_count++;
        g_mock_bk_5g_enable = *(const uint8_t *)buf;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_AP_CH_BAND) {
        memcpy(g_mock_bk_ap_pair, buf, 2);
        return 2;
    }
    if (bkid == ZVP_BKID_WIFI_NVRAM_GO) {
        g_mock_bk_nvram_go = *(const uint8_t *)buf;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_NVRAM_STA) {
        g_mock_bk_nvram_sta = *(const uint8_t *)buf;
        return 1;
    }
    if (bkid == ZVP_BKID_WIFI_BAND_MENU) {
        g_mock_bk_band_menu = *(const uint8_t *)buf;
        return 1;
    }
    return -1;
}

static void reset_mocks(void)
{
    g_mock_mounts = 0;
    g_mock_zvp_off_exists = false;
    g_mock_kill_path = NULL;
    g_mock_sleep_calls = 0;
    g_mock_netsvc_loaded = true;
    g_mock_caution_loaded = true;
    memcpy(g_mock_netsvc_mem, ZVP_ORIG_ISVALID_5GHZ, 4);
    g_mock_caution_mem[0] = ZVP_ORIG_CAUTION_BYTE;
    g_mock_bk_5g_enable = 0;
    g_mock_bk_ap_pair[0] = ZVP_WIFI_BAND_24G;
    g_mock_bk_ap_pair[1] = 6;
    g_mock_bk_nvram_go = 0x09u;
    g_mock_bk_nvram_sta = 0x09u;
    g_mock_bk_band_menu = 0x00u;
    g_mock_backup_write_count = 0;
    g_mock_bk_5g_enable_write_count = 0;
}

/* ============================================================================
 * 测试用例集
 * ========================================================================== */

static void test_gate_logic(void)
{
    zvp_gate_ops_t ops = { mock_get_mounts, mock_file_exists, mock_sleep };

    /* 1. 仅存在机内虚拟 A: 盘 (bit 0) 模拟未插 SD 卡，应静默禁用 */
    reset_mocks();
    g_mock_mounts = ZVP_FSYS_DRIVE_A_BIT;
    assert(zvp_gate_evaluate_boot(&ops, 300, 100) == ZVP_GATE_DISABLED_NO_CARD);
    assert(g_mock_sleep_calls == 3);

    /* 2. 有 SD 卡 (B: bit 1) 但存在 B:/ZVP_OFF 应熔断禁用 */
    reset_mocks();
    g_mock_mounts = ZVP_FSYS_DRIVE_A_BIT | ZVP_FSYS_DRIVE_B_BIT;
    g_mock_zvp_off_exists = true;
    g_mock_kill_path = "B:/ZVP_OFF";
    assert(zvp_gate_evaluate_boot(&ops, 300, 100) == ZVP_GATE_DISABLED_KILL_FILE);

    /* 3. 兼容 Windows 新建空文件误存为 B:/ZVP_OFF.TXT 时同样应熔断禁用 */
    reset_mocks();
    g_mock_mounts = ZVP_FSYS_DRIVE_A_BIT | ZVP_FSYS_DRIVE_B_BIT;
    g_mock_zvp_off_exists = true;
    g_mock_kill_path = "B:/ZVP_OFF.TXT";
    assert(zvp_gate_evaluate_boot(&ops, 300, 100) == ZVP_GATE_DISABLED_KILL_FILE);

    /* 4. 存在 ZVP_UNINSTALL 时应优先判定彻底卸载模式 */
    reset_mocks();
    g_mock_mounts = ZVP_FSYS_DRIVE_A_BIT | ZVP_FSYS_DRIVE_B_BIT;
    g_mock_zvp_off_exists = true;
    g_mock_kill_path = "B:/ZVP_UNINSTALL";
    assert(zvp_gate_evaluate_boot(&ops, 300, 100) == ZVP_GATE_DISABLED_UNINSTALL);

    /* 5. 有 SD 卡 (A: + B:) 且无禁用文件应允许加载 */
    reset_mocks();
    g_mock_mounts = ZVP_FSYS_DRIVE_A_BIT | ZVP_FSYS_DRIVE_B_BIT;
    g_mock_zvp_off_exists = false;
    assert(zvp_gate_evaluate_boot(&ops, 300, 100) == ZVP_GATE_ALLOW);
}

static void test_wifi5g_channels_lazy_patch_and_recovery(void)
{
    reset_mocks();
    zvp_wifi5g_platform_ops_t pops = {
        mock_find_base, mock_read_mem, mock_write_mem, mock_backup_read, mock_backup_write
    };
    zvp_wifi5g_ctx_t wifi;
    zvp_wifi5g_init(&wifi, &pops);

    size_t ch_cnt = 0;
    const uint8_t *chs = zvp_wifi5g_get_channels(&ch_cnt);
    assert(ch_cnt == 10);
    assert(chs[0] == 0xFF && chs[1] == 36 && chs[9] == 165);
    assert(zvp_wifi5g_is_valid_channel(149));
    assert(!zvp_wifi5g_is_valid_channel(52)); /* 非白名单信道必须拒绝 */

    /* 模拟 libNetService.so 懒加载场景 */
    g_mock_netsvc_loaded = false;
    assert(zvp_wifi5g_set_enabled(&wifi, true) == 0);
    assert(wifi.state == ZVP_PATCH_WAIT_NETSVC);
    assert(!wifi.net_patched && wifi.caution_patched);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_5G);
    assert(g_mock_bk_ap_pair[1] == ZVP_WIFI_CH_AUTO);
    assert(g_mock_bk_nvram_go == ZVP_WIFI_NVRAM_CN_GO);
    assert(g_mock_bk_nvram_sta == ZVP_WIFI_NVRAM_CN_STA);
    assert(g_mock_bk_band_menu == 0x01u);
    assert(g_mock_bk_5g_enable_write_count == 0);

    /* libNetService.so 加载后 poll 自动补齐补丁 */
    g_mock_netsvc_loaded = true;
    zvp_wifi5g_poll(&wifi);
    assert(wifi.state == ZVP_PATCH_ACTIVE);
    assert(wifi.net_patched);
    assert(memcmp(g_mock_netsvc_mem, ZVP_PATCH_ISVALID_5GHZ, 4) == 0);
    assert(g_mock_caution_mem[0] == ZVP_PATCH_CAUTION_BYTE);

    /* 选择信道 149 并验证写入 pair={band=2, ch=149} */
    assert(zvp_wifi5g_select_channel(&wifi, 149) == 0);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_5G);
    assert(g_mock_bk_ap_pair[1] == 149);
    assert(g_mock_bk_5g_enable_write_count == 0);

    /* 关闭 5GHz 后再开启：先验证恢复至原厂 2.4GHz {1, 6} 及原厂 NVRAM 0x09/0x09/0x00 */
    assert(zvp_wifi5g_set_enabled(&wifi, false) == 0);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_24G);
    assert(g_mock_bk_ap_pair[1] == 6);
    assert(g_mock_bk_nvram_go == 0x09u);
    assert(g_mock_bk_nvram_sta == 0x09u);
    assert(g_mock_bk_band_menu == 0x00u);
    assert(zvp_wifi5g_set_enabled(&wifi, true) == 0);
    assert(g_mock_bk_nvram_go == ZVP_WIFI_NVRAM_CN_GO);
    assert(g_mock_bk_nvram_sta == ZVP_WIFI_NVRAM_CN_STA);

    /* 安全复位两步确认：超时重设 + 3秒内二次点击回滚原字节 */
    zvp_recovery_ctx_t rec;
    zvp_recovery_init(&rec);
    assert(zvp_recovery_trigger(&rec, &wifi, 1000) == ZVP_REC_ARMED);
    zvp_recovery_tick(&rec, 4100);
    assert(rec.phase == ZVP_REC_IDLE);

    assert(zvp_recovery_trigger(&rec, &wifi, 5000) == ZVP_REC_ARMED);
    assert(zvp_recovery_trigger(&rec, &wifi, 6200) == ZVP_REC_DONE);
    assert(!wifi.enabled && wifi.state == ZVP_PATCH_IDLE);
    assert(memcmp(g_mock_netsvc_mem, ZVP_ORIG_ISVALID_5GHZ, 4) == 0);
    assert(g_mock_caution_mem[0] == ZVP_ORIG_CAUTION_BYTE);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_24G);
    assert(g_mock_bk_ap_pair[1] == 6);
    assert(g_mock_bk_nvram_go == 0x09u);
    assert(g_mock_bk_nvram_sta == 0x09u);
    assert(g_mock_bk_band_menu == 0x00u);
    assert(g_mock_bk_5g_enable_write_count == 0);
}

static void test_wifi5g_byte_mismatch_guard(void)
{
    reset_mocks();
    zvp_wifi5g_platform_ops_t pops = {
        mock_find_base, mock_read_mem, mock_write_mem, mock_backup_read, mock_backup_write
    };
    zvp_wifi5g_ctx_t wifi;
    zvp_wifi5g_init(&wifi, &pops);

    /* 破坏原机器码（模拟未知固件偏移），必须拒绝写入并返回错误 */
    g_mock_netsvc_mem[0] = 0xAA;
    assert(zvp_wifi5g_set_enabled(&wifi, true) != 0);
    assert(wifi.state == ZVP_PATCH_ERROR);
    assert(g_mock_netsvc_mem[0] == 0xAA);
}

static void test_input_c1_and_flip(void)
{
    zvp_input_state_t st;
    zvp_input_init(&st, ZVP_KEY_C1_DEFAULT);

    /* 1. 默认短按独占模式：按下立即切换并吞掉事件，伴生散焦码 0x15010310 同样被吞掉 */
    assert(st.c1_mode == ZVP_C1_MODE_SHORT_EXCLUSIVE);
    uint32_t act = zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 1000);
    assert((act & ZVP_ACT_TOGGLE_MENU) && (act & ZVP_ACT_CONSUME_SYS));
    assert(st.menu_visible);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, true, 1004) == ZVP_ACT_CONSUME_SYS);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 1050) == ZVP_ACT_CONSUME_SYS);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, false, 1054) == ZVP_ACT_CONSUME_SYS);

    /* 再按关闭菜单 */
    act = zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 1500);
    assert((act & ZVP_ACT_TOGGLE_MENU) && (act & ZVP_ACT_CONSUME_SYS));
    assert(!st.menu_visible);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 1550) == ZVP_ACT_CONSUME_SYS);

    /* 2. 切换至长按模式：短按(200ms)透传，不弹出菜单 */
    zvp_input_set_c1_mode(&st, ZVP_C1_MODE_LONG_PRESS);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 1800) == ZVP_ACT_NONE);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 1950) == ZVP_ACT_NONE);
    assert(!st.menu_visible);

    /* 3. 长按模式：按住超 500ms 由 tick 唤醒，松手事件被吞掉 */
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 2000) == ZVP_ACT_NONE);
    assert(zvp_input_tick(&st, 2499) == ZVP_ACT_NONE);
    act = zvp_input_tick(&st, 2500);
    assert((act & ZVP_ACT_TOGGLE_MENU) && (act & ZVP_ACT_CONSUME_SYS));
    assert(st.menu_visible);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 2600) == ZVP_ACT_CONSUME_SYS);

    /* 4. 菜单开启时拦截方向键、OK键、滚轮与触摸，且 MENU 键不关闭菜单（只用 C1 开关） */
    assert(zvp_input_process_key(&st, ZVP_KEY_DOWN, true, 2700) == (ZVP_ACT_NAV_DOWN | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_LEFT, true, 2702) == (ZVP_ACT_NAV_LEFT | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_RIGHT, true, 2704) == (ZVP_ACT_NAV_RIGHT | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_ENTER, true, 2706) == (ZVP_ACT_NAV_ENTER | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_MENU, true, 2710) == ZVP_ACT_CONSUME_SYS);
    assert(st.menu_visible);
    assert(zvp_input_process_key(&st, ZVP_KEY_LEGACY_MENU, true, 2720) == ZVP_ACT_CONSUME_SYS);
    assert(st.menu_visible);
    assert(zvp_input_process_dial(&st, ZVP_KEY_DIAL_WHEEL, 1) == (ZVP_ACT_DIAL_NEXT | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_dial(&st, ZVP_KEY_DIAL_TOP, -1) == (ZVP_ACT_DIAL_PREV | ZVP_ACT_CONSUME_SYS));
    zvp_input_update_flip(&st, true);
    assert(zvp_input_is_ui_rotated_180(&st));
    uint32_t tact = zvp_input_process_touch(&st, 100, 50, true);
    assert((tact & ZVP_ACT_TOUCH_DOWN) && (tact & ZVP_ACT_CONSUME_SYS));
    assert(st.touch_x == 539 && st.touch_y == 429);
    zvp_input_process_touch(&st, 100, 50, false);

    /* 取消翻转跟随，坐标应保持原样，并校验 w1 高16位=X / 低16位=Y 解包 */
    zvp_input_set_auto_flip(&st, false);
    assert(!zvp_input_is_ui_rotated_180(&st));
    uint32_t packed_w1 = ((uint32_t)550u << 16) | 75u; /* 右上角开关胶囊 (x=550, y=75) */
    assert(ZVP_TOUCH_UNPACK_X(packed_w1) == 550);
    assert(ZVP_TOUCH_UNPACK_Y(packed_w1) == 75);
    zvp_input_process_touch(&st, ZVP_TOUCH_UNPACK_X(packed_w1), ZVP_TOUCH_UNPACK_Y(packed_w1), true);
    assert(st.touch_x == 550 && st.touch_y == 75);
    assert(ZVP_EV_ENTER_PRESS == 0x15010042u);
    assert(ZVP_EV_ENTER_RELEASE == 0x15010041u);
}

static void test_wifi5g_dirty_backup_self_heal(void)
{
    reset_mocks();
    /* 模拟旧版插件遗留的脏数据 {0x00, 0x01} */
    g_mock_bk_ap_pair[0] = 0x00u;
    g_mock_bk_ap_pair[1] = 0x01u;
    zvp_wifi5g_platform_ops_t pops = {
        mock_find_base, mock_read_mem, mock_write_mem, mock_backup_read, mock_backup_write
    };
    zvp_wifi5g_ctx_t wifi;
    zvp_wifi5g_init(&wifi, &pops);

    /* 开机心跳 poll 自动把非法 {0x00, 0x01} 自愈回 {band=1, ch=0xFF} */
    zvp_wifi5g_poll(&wifi);
    assert(wifi.backup_initialized);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_24G);
    assert(g_mock_bk_ap_pair[1] == ZVP_WIFI_CH_AUTO);
    assert(g_mock_bk_5g_enable_write_count == 0);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void test_mini_widget_touch_isolation(void)
{
    zvp_input_state_t st;
    zvp_input_init(&st, ZVP_KEY_C1_DEFAULT);
    assert(!st.menu_visible);

    /* 悬浮窗显示在左上角 (10, 10, 252x46) */
    zvp_input_set_mini_widget(&st, true, 10, 10, 252, 46);
    assert(zvp_input_point_in_mini(&st, 10, 10));
    assert(zvp_input_point_in_mini(&st, 261, 55));
    assert(!zvp_input_point_in_mini(&st, 262, 55));
    assert(!zvp_input_point_in_mini(&st, 320, 240));

    /* 1. 悬浮窗外点击：必须完全透传 ZVP_ACT_NONE 给索尼触摸对焦 */
    assert(zvp_input_process_touch(&st, 320, 240, true) == ZVP_ACT_NONE);
    assert(!st.mini_touch_active);
    assert(zvp_input_process_touch(&st, 320, 240, false) == ZVP_ACT_NONE);

    /* 2. 悬浮窗内点击：必须独占消费 DOWN -> MOVE -> UP */
    uint32_t d = zvp_input_process_touch(&st, 50, 30, true);
    assert((d & ZVP_ACT_CONSUME_SYS) && (d & ZVP_ACT_TOUCH_DOWN));
    assert(st.mini_touch_active && st.mini_suppress_release);
    assert(st.touch_x == 50 && st.touch_y == 30);

    uint32_t m = zvp_input_process_touch(&st, 55, 32, true);
    assert((m & ZVP_ACT_CONSUME_SYS) && (m & ZVP_ACT_TOUCH_MOVE));

    uint32_t u = zvp_input_process_touch(&st, 55, 32, false);
    assert((u & ZVP_ACT_CONSUME_SYS) && (u & ZVP_ACT_TOUCH_UP));
    assert(!st.mini_touch_active);
    assert(st.mini_suppress_release);

    /* 3. 开启 180 度翻转时，物理右下角 (620, 460) -> 逻辑 (19, 19)，命中悬浮窗 */
    zvp_input_update_flip(&st, true);
    uint32_t d_flip = zvp_input_process_touch(&st, 620, 460, true);
    assert((d_flip & ZVP_ACT_CONSUME_SYS) && (d_flip & ZVP_ACT_TOUCH_DOWN));
    assert(st.touch_x == 19 && st.touch_y == 19);
    (void)zvp_input_process_touch(&st, 620, 460, false);

    /* 4. 菜单关闭且未开启悬浮窗时，左上角也必须透传 */
    zvp_input_set_mini_widget(&st, false, 10, 10, 252, 46);
    assert(zvp_input_process_touch(&st, 620, 460, true) == ZVP_ACT_NONE);
}

static void test_sound_wav_and_f0_conversion(void)
{
    /* 1. 构造 44.1kHz Stereo 16-bit PCM WAV (441 帧 ≈ 10ms) */
    const uint32_t frames = 441u;
    const uint32_t data_bytes = frames * 4u;
    const uint32_t wav_bytes = 44u + data_bytes;
    uint8_t wav[44u + 441u * 4u];
    memset(wav, 0, sizeof(wav));
    memcpy(wav, "RIFF", 4);
    put_le32(wav + 4, wav_bytes - 8u);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    put_le32(wav + 16, 16u);
    put_le16(wav + 20, 1u);
    put_le16(wav + 22, 2u);
    put_le32(wav + 24, 44100u);
    put_le32(wav + 28, 44100u * 4u);
    put_le16(wav + 32, 4u);
    put_le16(wav + 34, 16u);
    memcpy(wav + 36, "data", 4);
    put_le32(wav + 40, data_bytes);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)((i % 20u < 10u) ? 12000 : -12000);
        put_le16(wav + 44u + i * 4u, (uint16_t)s);
        put_le16(wav + 44u + i * 4u + 2u, (uint16_t)s);
    }

    zvp_wav_format_t fmt;
    assert(zvp_wav_parse(wav, sizeof(wav), &fmt) == 0);
    assert(fmt.channels == 2u && fmt.sample_rate == 44100u && fmt.bits_per_sample == 16u);
    assert(fmt.frames == frames);

    uint8_t f0[4096];
    size_t f0_len = 0u;
    assert(zvp_wav_to_f0(wav, sizeof(wav), f0, sizeof(f0), &f0_len) == 0);
    /* 10ms @ 24kHz = 240 samples -> ceil(240 / 31) = 8 blocks = 256 bytes */
    assert(f0_len == 8u * 32u);
    for (size_t b = 0; b < f0_len / 32u; b++) {
        assert(f0[b * 32u] <= 7u);
    }

    /* 2. 验证内置 F0 合成与 fopen 路径映射 */
    size_t b0 = zvp_sound_build_builtin_f0(0u, f0, sizeof(f0));
    assert(b0 > 0u && (b0 % 32u) == 0u);
    assert(strcmp(zvp_sound_remap_fopen_path("/usr/share/avsystemsound/zvp_slot_2d.bin"),
                  ZVP_SOUND_ACTIVE_BIN_PATH) == 0);
    assert(strcmp(zvp_sound_remap_fopen_path("/usr/share/avsystemsound/beeplist.txt"),
                  "/usr/share/avsystemsound/beeplist.txt") == 0);

    /* 3. 验证音频列表初始化、切换与写盘 */
    zvp_sound_mgr_t mgr;
    memset(&mgr, 0, sizeof(mgr));
    snprintf(mgr.override_dir, sizeof(mgr.override_dir), "/nonexistent_zvp_dir");
    snprintf(mgr.active_bin_path, sizeof(mgr.active_bin_path), "zvp_test_shutter.bin");
    zvp_sound_scan(&mgr);
    assert(mgr.count == 2u);
    assert(zvp_sound_select(&mgr, 0u));
    assert(mgr.prepared && mgr.last_f0_bytes > 0u);
    assert(zvp_sound_has_active_remap());
    zvp_sound_next(&mgr);
    assert(mgr.selected_idx == 1u);
    zvp_sound_prev(&mgr);
    assert(mgr.selected_idx == 0u);

    /* 4. 回归测试：SysSndData 布局 (ZVP_SYSSND_ADF_OFF == 10860) 与错位低地址指针防护 */
    static uint8_t mock_sys_snd[11248];
    memset(mock_sys_snd, 0, sizeof(mock_sys_snd));
    for (uint32_t sid = 0u; sid < 67u; sid++) {
        uint8_t *ent = mock_sys_snd + 140u + (size_t)sid * 160u;
        *(uint32_t *)(ent + 0u) = sid;
        snprintf((char *)(ent + 8u), 32u, "None");
    }
    /* 在旧错误偏移 10856 放入非指针小整数 2 (模拟 SoundTable[66].field_156)，并在 adfTable[0].isFixed 放 1 */
    *(uint32_t *)(mock_sys_snd + 10856u) = 2u;
    *(uint32_t *)(mock_sys_snd + 10860u + 12u) = 1u; /* adfTable[0].isFixed = 1 -> 旧代码在 i=1 时将其当指针 */
    *(uint32_t *)(mock_sys_snd + 10860u + 16u + 4u) = 0x100u; /* adfTable[1].unitHandle != 0 */
    /* adfTable[0] 指向 0x2d 槽位且 status=3，验证会安全跳过 0x2d 并选到 0x2e */
    uint8_t *slot2d = mock_sys_snd + 140u + (size_t)0x2du * 160u;
    memcpy(mock_sys_snd + 10860u + 0u, &slot2d, sizeof(slot2d));
    *(uint32_t *)(mock_sys_snd + 10860u + 8u) = 3u;
    /* adfTable[2] 放入越界野指针，验证边界防护不会解引用 */
    uintptr_t wild_ptr = 0x12345u;
    memcpy(mock_sys_snd + 10860u + 32u + 0u, &wild_ptr, sizeof(wild_ptr));
    *(uint32_t *)(mock_sys_snd + 10860u + 32u + 8u) = 3u;

    zvp_sound_on_sys_data(mock_sys_snd);
    mgr.slot_cursor = 0u;
    assert(zvp_sound_select(&mgr, 0u));
    assert(mgr.active_slot == 0x2eu);
    assert(strcmp((const char *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 8u), "zvp_slot_2e.bin") == 0);
    assert(*(uint32_t *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 4u) == 0x00070817u);
    assert(*(uint32_t *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 136u) == 2u);

    /* 5. 测试 zvp_sound_play_prepared 非阻塞边界与主机端模拟播音计数 */
    mgr.prepared = false;
    assert(!zvp_sound_play_prepared(&mgr, 1000u));
    assert(zvp_sound_prepare_current(&mgr));
#ifdef _WIN32
    assert(zvp_sound_play_prepared(&mgr, 1000u));
    assert(mgr.play_count == 1u);
#endif

    /* 6. 校验整理后的 assets/sounds 全部示例 WAV 均可成功扫描与转码为 Sony F0，
     *    并模拟 4 个音效多轮回绕与 7 个音效（>4）淘汰 LRU 轮换不溢出、不阻塞 */
    const char *sample_dirs[] = { "assets/sounds", "../assets/sounds" };
    bool checked_samples = false;
    for (size_t d = 0u; d < 2u; d++) {
        zvp_sound_mgr_t smgr;
        memset(&smgr, 0, sizeof(smgr));
        snprintf(smgr.override_dir, sizeof(smgr.override_dir), "%s", sample_dirs[d]);
        snprintf(smgr.active_bin_path, sizeof(smgr.active_bin_path), "%s", "zvp_test_sample.bin");
        zvp_sound_scan(&smgr);
        if (smgr.count > 2u) {
            assert(smgr.count == 7u);
            /* 先在清理后的 mock_sys_snd 上测试 4 个音效多轮切换命中复用（绝不超 4 个且不淘汰） */
            memset(mock_sys_snd, 0, sizeof(mock_sys_snd));
            for (uint32_t sid = 0u; sid < 67u; sid++) {
                uint8_t *ent = mock_sys_snd + 140u + (size_t)sid * 160u;
                *(uint32_t *)(ent + 0u) = sid;
                snprintf((char *)(ent + 8u), 32u, "None");
            }
            zvp_sound_on_sys_data(mock_sys_snd);

            /* 连续切并模拟进入 adfTable (indices 8..11) 前 4 个，再切 8 轮(32次)验证始终命中原 4 槽位且零淘汰 */
            uint8_t round1_slots[4];
            for (uint32_t idx = 0u; idx < 4u; idx++) {
                assert(zvp_sound_select(&smgr, idx));
                round1_slots[idx] = smgr.active_slot;
                uint8_t *ent_addr = mock_sys_snd + 140u + (size_t)smgr.active_slot * 160u;
                uint8_t *adf_ent = mock_sys_snd + 10860u + (size_t)(8u + idx) * 16u;
                memcpy(adf_ent + 0u, &ent_addr, sizeof(ent_addr));
                if (sizeof(ent_addr) <= 4u) {
                    adf_ent[6u] = (uint8_t)(9u + idx);
                }
                *(uint32_t *)(adf_ent + 8u) = 3u;
            }
            assert(smgr.evict_count == 0u);
            for (uint32_t r = 0u; r < 8u; r++) {
                for (uint32_t idx = 0u; idx < 4u; idx++) {
                    assert(zvp_sound_select(&smgr, idx));
                    assert(smgr.active_slot == round1_slots[idx]);
                }
            }
            assert(smgr.evict_count == 0u);

            /* 场景 A: 4 槽均已进 adfTable 后，仅切不听连续切换第 5~7 个：
             * 仅第 5 个淘汰 1 个最旧槽位(evict_count=1)，第 6、7 个原地复用该未播槽位不再增加淘汰 */
            for (uint32_t idx = 4u; idx < smgr.count; idx++) {
                assert(zvp_sound_select(&smgr, idx));
                assert(smgr.prepared);
            }
            assert(smgr.evict_count == 1u);

            /* 场景 B: 模拟每次切换后真实播放(进 adfTable) 2 轮共 7 个音色，验证活跃 adf 始终 <= 4 */
            uint32_t evict_before_b = smgr.evict_count;
            for (uint32_t r = 0u; r < 2u; r++) {
                for (uint32_t idx = 0u; idx < smgr.count; idx++) {
                    assert(zvp_sound_select(&smgr, idx));
                    assert(smgr.prepared);
                    assert(smgr.last_f0_bytes > 0u && (smgr.last_f0_bytes % ZVP_SOUND_F0_BLOCK_BYTES) == 0u);
                    bool found_in_cache = false;
                    for (uint32_t c = 0u; c < ZVP_SOUND_CACHE_SLOTS; c++) {
                        if (smgr.slot_cache[c].occupied && smgr.slot_cache[c].slot == smgr.active_slot) {
                            found_in_cache = true;
                        }
                    }
                    assert(found_in_cache);

                    /* 模拟试听/快门播放将该槽位加载进 adfTable，并统计当前活跃 adf 数始终 <= 4 */
                    uint8_t *ent_addr = mock_sys_snd + 140u + (size_t)smgr.active_slot * 160u;
                    bool in_adf = false;
                    uint32_t free_adf = 99u;
                    uint32_t active_adf_cnt = 0u;
                    for (uint32_t a = 0u; a < 20u; a++) {
                        uint8_t *adf_ent = mock_sys_snd + 10860u + (size_t)a * 16u;
                        uint32_t st = *(uint32_t *)(adf_ent + 8u);
                        void *ptr = NULL;
                        memcpy(&ptr, adf_ent + 0u, sizeof(ptr));
                        if (st != 0u) {
                            active_adf_cnt++;
                            if (ptr == ent_addr) {
                                in_adf = true;
                            }
                        } else if (free_adf == 99u && a >= 8u) {
                            free_adf = a;
                        }
                    }
                    if (!in_adf) {
                        assert(free_adf < 20u);
                        uint8_t *adf_ent = mock_sys_snd + 10860u + (size_t)free_adf * 16u;
                        memcpy(adf_ent + 0u, &ent_addr, sizeof(ent_addr));
                        if (sizeof(ent_addr) <= 4u) {
                            adf_ent[6u] = (uint8_t)(free_adf + 1u);
                        }
                        *(uint32_t *)(adf_ent + 8u) = 3u;
                        active_adf_cnt++;
                    }
                    assert(active_adf_cnt <= 4u);
                }
            }
            assert(smgr.evict_count > evict_before_b);
            checked_samples = true;
            break;
        }
    }
    assert(checked_samples);
    zvp_sound_on_sys_data(mock_sys_snd);

    /* 7. 验证 zvp_sound_reset() 还原 SoundTable 槽位、关闭重映射、重置选择并删除缓存文件 */
    zvp_sound_reset(&mgr);
    assert(!zvp_sound_has_active_remap());
    assert(!mgr.prepared);
    assert(mgr.play_count == 0u);
    assert(mgr.last_f0_bytes == 0u);
    assert(strcmp((const char *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 8u), "None") == 0);
    assert(*(uint32_t *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 4u) == 0u);
    assert(*(uint32_t *)(mock_sys_snd + 140u + (size_t)0x2eu * 160u + 136u) == 0u);
    assert(fopen("zvp_test_shutter.bin", "rb") == NULL);

    remove("zvp_test_sample.bin");
    remove("zvp_test_shutter.bin");
}

static bool g_mock_extra_reset_called = false;
static int mock_extra_reset_cb(void *user_data)
{
    int *flag = (int *)user_data;
    g_mock_extra_reset_called = true;
    if (flag) {
        *flag = 1;
    }
    return 0;
}

static void test_full_recovery_callback(void)
{
    reset_mocks();
    zvp_wifi5g_platform_ops_t pops = {
        mock_find_base, mock_read_mem, mock_write_mem, mock_backup_read, mock_backup_write
    };
    zvp_wifi5g_ctx_t wifi;
    zvp_wifi5g_init(&wifi, &pops);

    /* 即使未开启 5GHz，仅打开其他功能时触发全量安全复位，也应触发扩展全量回退并成功 */
    int extra_flag = 0;
    g_mock_extra_reset_called = false;
    zvp_recovery_ctx_t rec;
    zvp_recovery_init(&rec);
    zvp_recovery_set_full_reset_cb(&rec, mock_extra_reset_cb, &extra_flag);

    assert(zvp_recovery_trigger(&rec, &wifi, 1000) == ZVP_REC_ARMED);
    assert(!g_mock_extra_reset_called);
    assert(zvp_recovery_trigger(&rec, &wifi, 2000) == ZVP_REC_DONE);
    assert(g_mock_extra_reset_called);
    assert(extra_flag == 1);
    assert(wifi.state == ZVP_PATCH_IDLE);
}

static uint8_t g_bg_guard_buf[64];
static size_t  g_bg_guard_len = 0u;
static char    g_bg_dmode[16] = "3p\n";
static char    g_bg_preload[128] = "/usr/tool/libmcheckb.so:/tmp/libzve10plugin.so";

static int mock_bg_read(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (strcmp(path, "mock_guard") == 0 && g_bg_guard_len > 0u) {
        size_t n = (g_bg_guard_len < cap) ? g_bg_guard_len : cap;
        memcpy(buf, g_bg_guard_buf, n);
        if (out_len) *out_len = n;
        return 0;
    }
    return -1;
}

static int mock_bg_write(const char *path, const void *buf, size_t len)
{
    if (strcmp(path, "mock_guard") == 0 && len <= sizeof(g_bg_guard_buf)) {
        memcpy(g_bg_guard_buf, buf, len);
        g_bg_guard_len = len;
        return 0;
    }
    if (strcmp(path, "mock_dmode") == 0 && len < sizeof(g_bg_dmode)) {
        memcpy(g_bg_dmode, buf, len);
        g_bg_dmode[len] = '\0';
        return 0;
    }
    if (strcmp(path, "mock_preload") == 0 && len < sizeof(g_bg_preload)) {
        memcpy(g_bg_preload, buf, len);
        g_bg_preload[len] = '\0';
        return 0;
    }
    return -1;
}

static int mock_bg_unlink(const char *path)
{
    if (strcmp(path, "mock_guard") == 0) {
        g_bg_guard_len = 0u;
        return 0;
    }
    return -1;
}

static void test_boot_guard_and_backup_rollback(void)
{
    zvp_boot_guard_ops_t bg_ops = { mock_bg_read, mock_bg_write, mock_bg_unlink };
    zvp_boot_guard_state_t st;

    /* 1. 正常启动并在存活后清零：连续反复启动也不会触发熔断 */
    g_bg_guard_len = 0u;
    snprintf(g_bg_dmode, sizeof(g_bg_dmode), "3p\n");
    snprintf(g_bg_preload, sizeof(g_bg_preload), "/usr/tool/libmcheckb.so:/tmp/libzve10plugin.so");
    for (int i = 0; i < 4; i++) {
        assert(!zvp_boot_guard_enter(&bg_ops, "mock_guard", "mock_dmode", "mock_preload", 2u, &st));
        assert(st.fail_count == 1u && st.trip_count == 0u);
        zvp_boot_guard_mark_healthy(&bg_ops, "mock_guard");
    }
    assert(strcmp(g_bg_dmode, "3p\n") == 0);

    /* 2. 连续两次未完成健康清零（模拟开机崩溃/卡死重启），第 3 次必须自动熔断清 preload */
    assert(!zvp_boot_guard_enter(&bg_ops, "mock_guard", "mock_dmode", "mock_preload", 2u, &st));
    assert(st.fail_count == 1u);
    assert(!zvp_boot_guard_enter(&bg_ops, "mock_guard", "mock_dmode", "mock_preload", 2u, &st));
    assert(st.fail_count == 2u);
    assert(zvp_boot_guard_enter(&bg_ops, "mock_guard", "mock_dmode", "mock_preload", 2u, &st));
    assert(st.fail_count == 0u && st.trip_count == 1u);
    assert(strcmp(g_bg_dmode, "3\n") == 0);
    assert(strcmp(g_bg_preload, "\n") == 0);

    /* 3. 验证门禁还原 Backup 共用 helper 准确写回原厂 2.4GHz 与 NVRAM 参数 */
    reset_mocks();
    zvp_wifi5g_platform_ops_t pops = {
        mock_find_base, mock_read_mem, mock_write_mem, mock_backup_read, mock_backup_write
    };
    g_mock_bk_ap_pair[0] = ZVP_WIFI_BAND_5G;
    g_mock_bk_ap_pair[1] = 149u;
    g_mock_bk_nvram_go = ZVP_WIFI_NVRAM_CN_GO;
    g_mock_bk_nvram_sta = ZVP_WIFI_NVRAM_CN_STA;
    g_mock_bk_band_menu = 1u;
    uint8_t orig_pair[2] = { ZVP_WIFI_BAND_24G, 6u };
    assert(zvp_recovery_restore_backup_vals(&pops, true, 0x09u, 0x09u, 0x00u, orig_pair) == 0);
    assert(g_mock_bk_ap_pair[0] == ZVP_WIFI_BAND_24G && g_mock_bk_ap_pair[1] == 6u);
    assert(g_mock_bk_nvram_go == 0x09u && g_mock_bk_nvram_sta == 0x09u && g_mock_bk_band_menu == 0u);
}

int main(void)
{
    test_gate_logic();
    test_wifi5g_channels_lazy_patch_and_recovery();
    test_wifi5g_byte_mismatch_guard();
    test_wifi5g_dirty_backup_self_heal();
    test_input_c1_and_flip();
    test_mini_widget_touch_isolation();
    test_sound_wav_and_f0_conversion();
    test_full_recovery_callback();
    test_boot_guard_and_backup_rollback();
    printf("[通过] zvp 全部核心逻辑单元测试通过！\n");
    return 0;
}
