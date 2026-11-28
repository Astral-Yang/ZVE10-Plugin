#include "zvp_core.h"
#include "zvp_abi.h"
#include "zvp_sound.h"
#include "zvp_aspect.h"
#include "zvp_craw.h"
#include "zvp_liro_blob.h"
#include "zvp_lut.h"
#include "zvp_lut_blobs.h"

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

    /* 2. 切换至长按模式：短按(150ms) 按下期间主码与伴生散焦码均暂扣(DEFER)，松手触发回放(REPLAY)激活原厂 */
    zvp_input_set_c1_mode(&st, ZVP_C1_MODE_LONG_PRESS);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 1800) == (ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, true, 1802) == (ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 1950) == ZVP_ACT_REPLAY_C1);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, false, 1952) == ZVP_ACT_NONE);
    assert(!st.menu_visible);

    /* 3. 长按模式：按住超 500ms 由 tick 唤醒，松手事件及伴生散焦事件全被吞掉，绝不误触原厂 */
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, true, 2000) == (ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, true, 2002) == (ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS));
    assert(zvp_input_tick(&st, 2499) == ZVP_ACT_NONE);
    act = zvp_input_tick(&st, 2500);
    assert((act & ZVP_ACT_TOGGLE_MENU) && (act & ZVP_ACT_CONSUME_SYS));
    assert(st.menu_visible);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFAULT, false, 2600) == ZVP_ACT_CONSUME_SYS);
    assert(zvp_input_process_key(&st, ZVP_KEY_C1_DEFOCUS_AUX, false, 2602) == ZVP_ACT_CONSUME_SYS);

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

static void test_aspect_ratio_layout_and_state(void)
{
    zvp_aspect_ctx_t ctx;
    zvp_aspect_init(&ctx);

    size_t pcnt = 0;
    const zvp_aspect_preset_t *presets = zvp_aspect_get_presets(&pcnt);
    assert(presets != NULL && pcnt == ZVP_ASPECT_PRESET_COUNT);
    assert(!ctx.enabled);
    assert(ctx.style == ZVP_ASPECT_STYLE_DIM_45);
    assert(ctx.guide == ZVP_ASPECT_GUIDE_BORDER);
    assert(ctx.par_mode == ZVP_ASPECT_PAR_AUTO);

    /* 1. 默认去底栏实际拍摄画面 (0, 0, 640, 428) 居中校验（绝不覆盖底部 52px 参数栏） */
    assert(ctx.layout.valid);
    assert(ctx.layout.view_rect.x == 0 && ctx.layout.view_rect.y == 0);
    assert(ctx.layout.view_rect.w == 640 && ctx.layout.view_rect.h == 428);
    /* 默认 21:9 (7:3)：640 * 3 / 7 = 274 -> y = (428 - 274)/2 = 77，上下黑边各 77 */
    assert(ctx.layout.frame_rect.x == 0 && ctx.layout.frame_rect.w == 640);
    assert(ctx.layout.frame_rect.y == 77 && ctx.layout.frame_rect.h == 274);
    assert(ctx.layout.bar_top.h == 77 && ctx.layout.bar_bottom.y == 351 && ctx.layout.bar_bottom.h == 77);

    /* 在 640x428 下 16:9 居中：640x360 -> y=34，底部 y=394 (<428)；1:1 正方形 -> 428x428 (左右各 106) */
    zvp_aspect_rect_t lv_3_2 = { 0, 0, 640, 428 };
    zvp_aspect_layout_t ly;
    zvp_aspect_compute_layout(&lv_3_2, 1, 1, 16, 9, false, &ly);
    assert(ly.frame_rect.x == 0 && ly.frame_rect.y == 34);
    assert(ly.frame_rect.w == 640 && ly.frame_rect.h == 360);
    assert(ly.bar_bottom.y + ly.bar_bottom.h == 428);

    zvp_aspect_compute_layout(&lv_3_2, 1, 1, 1, 1, false, &ly);
    assert(ly.frame_rect.x == 106 && ly.frame_rect.y == 0);
    assert(ly.frame_rect.w == 428 && ly.frame_rect.h == 428);
    assert(ly.bar_left.w == 106 && ly.bar_right.w == 106);

    /* 2. 16:9 物理面板映射验证及 180° 翻转时不重复倒置（由底层 blit 统一整屏翻转） */
    zvp_aspect_rect_t full = { 0, 0, 640, 480 };
    zvp_aspect_compute_layout(&full, 4, 3, 16, 9, false, &ly);
    assert(ly.frame_rect.w == 640 && ly.frame_rect.h == 480);

    zvp_aspect_compute_layout(&lv_3_2, 1, 1, 21, 9, true, &ly);
    assert(ly.view_rect.x == 0 && ly.view_rect.y == 0 && ly.view_rect.h == 428);
    assert(ly.frame_rect.y == 77 && ly.frame_rect.h == 274);

    /* 3. 四档取景基准切换验证：自动 -> 实拍去底栏 -> 全屏无底栏 -> 16:9 画面 */
    zvp_aspect_select_preset(&ctx, 5u); /* 16:9 */
    zvp_aspect_cycle_par_mode(&ctx, 1); /* 实拍去底栏 640x428 */
    zvp_aspect_recompute(&ctx, false);
    assert(ctx.layout.view_rect.h == 428 && ctx.layout.frame_rect.y == 34 && ctx.layout.frame_rect.h == 360);
    zvp_aspect_cycle_par_mode(&ctx, 1); /* 全屏无底栏 640x480 */
    zvp_aspect_recompute(&ctx, false);
    assert(ctx.layout.view_rect.h == 480 && ctx.layout.frame_rect.y == 60 && ctx.layout.frame_rect.h == 360);
    zvp_aspect_cycle_par_mode(&ctx, 1); /* 16:9 画面 (0,34,640,360) */
    zvp_aspect_select_preset(&ctx, 4u); /* 21:9 */
    zvp_aspect_recompute(&ctx, false);
    assert(ctx.layout.view_rect.y == 34 && ctx.layout.view_rect.h == 360);
    assert(ctx.layout.frame_rect.y == 77 && ctx.layout.frame_rect.h == 274);
    zvp_aspect_cycle_par_mode(&ctx, 1); /* 回到自动 */
    assert(ctx.par_mode == ZVP_ASPECT_PAR_AUTO);

    /* 4. 验证样式/辅助线/原厂 MENU/回放上下文隐藏隔离 */
    zvp_aspect_set_enabled(&ctx, true);
    zvp_aspect_update_camera_ui_state(&ctx, false, false);
    assert(zvp_aspect_should_display(&ctx, false));

    zvp_aspect_update_camera_ui_state(&ctx, true, false);
    assert(!zvp_aspect_should_display(&ctx, false));
    assert(zvp_aspect_should_display(&ctx, true)); /* 插件菜单打开允许底部实时预览 */

    zvp_aspect_update_camera_ui_state(&ctx, false, true);
    assert(!zvp_aspect_should_display(&ctx, false));

    zvp_aspect_cycle_style(&ctx, 1);
    assert(ctx.style == ZVP_ASPECT_STYLE_DIM_75);
    assert(zvp_aspect_style_opa(ZVP_ASPECT_STYLE_SOLID_BLACK) == 255u);
    zvp_aspect_cycle_guide(&ctx, -1);
    assert(ctx.guide == ZVP_ASPECT_GUIDE_NONE);
    zvp_aspect_cycle_par_mode(&ctx, 3);
    assert(ctx.par_mode == ZVP_ASPECT_PAR_4_3);

    /* 5. 安全复位后回到默认关闭，同时保留硬件面板探测快照 */
    zvp_aspect_update_vdf_rect(&ctx, 50, 0, 540, 480);
    zvp_aspect_update_panel_aspect(&ctx, true, 0u, 2u);
    zvp_aspect_reset_defaults(&ctx);
    assert(!ctx.enabled);
    assert(ctx.preset_idx == 4u);
    assert(ctx.style == ZVP_ASPECT_STYLE_DIM_45);
    assert(ctx.hw.vdf_rect_valid && ctx.hw.vdf_rect.w == 540);
    assert(ctx.hw.panel_aspect_valid && ctx.hw.panel_is_16_9);
}

/* ============================================================================
 * 2K cRAW Pure-RAM 控制器与 LIRO SHM/VTable 状态机单元测试
 * ========================================================================== */

static uint8_t  g_mock_craw_shm[ZVP_CRAW_SHM_REGION_SIZE];
static uint8_t  g_mock_craw_code[ZVP_CRAW_CODE_MAX_SIZE];
static uint8_t  g_mock_craw_dbg[ZVP_CRAW_DBG_SIZE];
static uint8_t  g_mock_craw_hdr_page[ZVP_CRAW_FILE_HDR_BYTES];
static uint8_t  g_mock_craw_wav_page[ZVP_CRAW_FILE_HDR_BYTES];
static uint32_t g_mock_slot_seq_movie  = ZVP_AVCAM_ORIG_SEQ_MOVIE;
static uint32_t g_mock_slot_rdcw0_s7   = ZVP_AVCAM_ORIG_RDCW0_S7;
static uint32_t g_mock_slot_camrdc_cfg = ZVP_AVCAM_ORIG_CAMRDC_CFG;
static uint32_t g_mock_slot_camaif_s4  = ZVP_AVCAM_ORIG_CAMAIF_S4;
static uint32_t g_mock_slot_ifmt_s4    = ZVP_AVCAM_ORIG_IFMT_S4;
static uint32_t g_mock_slot_ifmt_s10   = ZVP_AVCAM_ORIG_IFMT_S10;
static uint32_t g_mock_slot_dfe_rdc10  = ZVP_AVCAM_ORIG_DFE_RDC_S10;
static uint32_t g_mock_slot_dfe_rdccfg = ZVP_AVCAM_ORIG_DFE_RDC_CFG;
static uint32_t g_mock_slot_dfe_main   = ZVP_AVCAM_ORIG_DFE_MAIN;
static uint32_t g_mock_slot_state_hdl  = ZVP_AVCAM_ORIG_STATE_HDL;
static uint32_t g_mock_slot_dfe_rdcw0  = ZVP_AVCAM_ORIG_DFE_RDCW0;
static uint32_t g_mock_ext_rdcw1_s7    = ZVP_AVCAM_EXT_ORIG_RDCW1_S7;
static uint32_t g_mock_ext_rdcw1_s8    = ZVP_AVCAM_EXT_ORIG_RDCW1_S8;
static uint32_t g_mock_ext_spl_ee_s2   = ZVP_AVCAM_EXT_ORIG_SPL_EE_S2;
static uint32_t g_mock_ext_ee_s2       = ZVP_AVCAM_EXT_ORIG_EE_S2;
static uint32_t g_mock_ext_spl_ee_s17  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S17;
static uint32_t g_mock_ext_ee_s17      = ZVP_AVCAM_EXT_ORIG_EE_S17;
static uint32_t g_mock_ext_spl_ee_s22  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S22;
static uint32_t g_mock_ext_spl_cap_s23 = ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23;

static uint16_t g_mock_dmm_next_uh = 100u;
static uint32_t g_mock_dmm_held_count = 0u;
static uint32_t g_mock_fsys_written_bytes = 0u;
static uint32_t g_mock_fsys_wdirect_calls = 0u;

static void *mock_craw_map_pa(uint32_t pa, size_t len, size_t *avail)
{
    if (pa >= ZVP_CRAW_SHM_PA && (pa + len) <= (ZVP_CRAW_SHM_PA + sizeof(g_mock_craw_shm))) {
        *avail = sizeof(g_mock_craw_shm) - (pa - ZVP_CRAW_SHM_PA);
        return &g_mock_craw_shm[pa - ZVP_CRAW_SHM_PA];
    }
    if (pa >= ZVP_CRAW_CODE_PA && (pa + len) <= (ZVP_CRAW_CODE_PA + sizeof(g_mock_craw_code))) {
        *avail = sizeof(g_mock_craw_code) - (pa - ZVP_CRAW_CODE_PA);
        return &g_mock_craw_code[pa - ZVP_CRAW_CODE_PA];
    }
    if (pa >= ZVP_CRAW_DBG_PA && (pa + len) <= (ZVP_CRAW_DBG_PA + sizeof(g_mock_craw_dbg))) {
        *avail = sizeof(g_mock_craw_dbg) - (pa - ZVP_CRAW_DBG_PA);
        return &g_mock_craw_dbg[pa - ZVP_CRAW_DBG_PA];
    }
    if (pa >= 0x1A000000u && (pa + len) <= (0x1A000000u + sizeof(g_mock_craw_hdr_page))) {
        *avail = sizeof(g_mock_craw_hdr_page) - (pa - 0x1A000000u);
        return &g_mock_craw_hdr_page[pa - 0x1A000000u];
    }
    if (pa >= 0x3E666000u && (pa + len) <= (0x3E666000u + sizeof(g_mock_craw_wav_page))) {
        *avail = sizeof(g_mock_craw_wav_page) - (pa - 0x3E666000u);
        return &g_mock_craw_wav_page[pa - 0x3E666000u];
    }
    struct { uint32_t pa; uint32_t *ptr; } slots[] = {
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_SEQ_MOVIE),  &g_mock_slot_seq_movie },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_RDCW0_S7),   &g_mock_slot_rdcw0_s7 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_CAMRDC_CFG), &g_mock_slot_camrdc_cfg },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_CAMAIF_S4),  &g_mock_slot_camaif_s4 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_IFMT_S4),    &g_mock_slot_ifmt_s4 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_IFMT_S10),   &g_mock_slot_ifmt_s10 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_DFE_RDC_S10),&g_mock_slot_dfe_rdc10 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_DFE_RDC_CFG),&g_mock_slot_dfe_rdccfg },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_DFE_MAIN),   &g_mock_slot_dfe_main },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_STATE_HDL),  &g_mock_slot_state_hdl },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_SLOT_DFE_RDCW0),  &g_mock_slot_dfe_rdcw0 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_RDCW1_S7),    &g_mock_ext_rdcw1_s7 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_RDCW1_S8),    &g_mock_ext_rdcw1_s8 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_SPL_EE_S2),   &g_mock_ext_spl_ee_s2 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_EE_S2),       &g_mock_ext_ee_s2 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_SPL_EE_S17),  &g_mock_ext_spl_ee_s17 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_EE_S17),      &g_mock_ext_ee_s17 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_SPL_EE_S22),  &g_mock_ext_spl_ee_s22 },
        { ZVP_AVCAM_VMA_TO_PA(ZVP_AVCAM_EXT_SPL_CAP_S23), &g_mock_ext_spl_cap_s23 },
    };
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        if (pa == slots[i].pa && len == sizeof(uint32_t)) {
            *avail = sizeof(uint32_t);
            return slots[i].ptr;
        }
    }
    return NULL;
}

static int mock_craw_phys_read(uint32_t pa, void *out_buf, size_t len)
{
    size_t avail = 0u;
    void *p = mock_craw_map_pa(pa, len, &avail);
    if (!p || avail < len) return -1;
    memcpy(out_buf, p, len);
    return 0;
}

static int mock_craw_phys_write(uint32_t pa, const void *buf, size_t len)
{
    size_t avail = 0u;
    void *p = mock_craw_map_pa(pa, len, &avail);
    if (!p || avail < len) return -1;
    memcpy(p, buf, len);
    return 0;
}

static int mock_craw_dmm_alloc(uint32_t unit_type, uint16_t *out_uhdl, uint32_t *out_pa, uint32_t *out_sz)
{
    uint32_t pa = 0u, sz = 0u;
    switch (unit_type) {
    case 0x0003c2u: pa = 0x1A000000u; sz = 29360128u; break;
    case 0x081227u: pa = 0x3E666000u; sz =   524288u; break;
    case 0x081228u: pa = 0x1ADD2000u; sz =  1048576u; break;
    case 0x081229u: pa = 0x1F900000u; sz =  1572864u; break;
    case 0x08122au: pa = 0x24984000u; sz =  2097152u; break;
    case 0x08122bu: pa = 0x25000000u; sz =  2621440u; break;
    case 0x08122cu: pa = 0x1AED2000u; sz =  3145728u; break;
    default: return -2;
    }
    *out_uhdl = g_mock_dmm_next_uh++;
    *out_pa = pa;
    *out_sz = sz;
    g_mock_dmm_held_count++;
    return 0;
}

static int mock_craw_dmm_free(uint16_t uhdl)
{
    (void)uhdl;
    if (g_mock_dmm_held_count > 0u) g_mock_dmm_held_count--;
    return 1;
}

static int mock_craw_fsys_open(const char *path, int *out_fh)
{
    (void)path;
    *out_fh = 77;
    return 0;
}

static int mock_craw_fsys_write(int fh, uint32_t align_sz,
                                const zvp_craw_extent_t *exts, uint32_t count,
                                uint32_t total_bytes)
{
    assert(fh == 77);
    assert(align_sz == ZVP_CRAW_DIRECT_ALIGN);
    assert(exts != NULL && count >= 1u);
    g_mock_fsys_written_bytes += total_bytes;
    return (int)total_bytes;
}

static int mock_craw_fsys_close(int fh)
{
    assert(fh == 77);
    return 0;
}

static int mock_craw_fsys_wdirect(const char *path, uint32_t total_bytes,
                                  const zvp_craw_extent_t *exts, uint32_t count)
{
    (void)path;
    assert(exts != NULL && count == 1u && total_bytes == 4096u);
    g_mock_fsys_wdirect_calls++;
    return 0;
}

static int mock_craw_fsys_free_mb(uint32_t *out_free_mb)
{
    *out_free_mb = 4096u;
    return 0;
}

static uint64_t mock_craw_mono_ms(void)
{
    return 1000ull;
}

static void test_craw_logic(void)
{
    /* 1. 帧步长、4KB 对齐与容器头格式验证 */
    assert(zvp_craw_calc_frame_bytes(1920u, 1088u, 2u) == 2088960u);
    assert((zvp_craw_calc_frame_bytes(1920u, 1088u, 2u) % ZVP_CRAW_PAGE_ALIGN) == 0u);
    assert((zvp_craw_calc_frame_bytes(1920u, 1080u, 2u) % ZVP_CRAW_PAGE_ALIGN) == 0u);

    uint8_t wav[44];
    zvp_craw_build_wav_header(wav, 48000u, 2u, 16u, 192000u);
    assert(memcmp(wav + 0, "RIFF", 4) == 0);
    assert(memcmp(wav + 8, "WAVEfmt ", 8) == 0);
    assert(memcmp(wav + 36, "data", 4) == 0);
    uint32_t byte_rate = 0u;
    memcpy(&byte_rate, wav + 28, 4);
    assert(byte_rate == 192000u);

    uint8_t raw_page[ZVP_CRAW_FILE_HDR_BYTES];
    zvp_craw_file_hdr_t fhdr = {
        .magic = ZVP_CRAW_FILE_HDR_MAGIC,
        .version = 1u,
        .width = 1920u,
        .height = 1088u,
        .bpp = 12u,
        .enc_type = 2u,
        .fps_num = 24000u,
        .fps_den = 1001u,
        .frame_bytes = 2088960u,
        .frame_count = 4u,
        .drop_count = 0u,
        .flags = 0u,
    };
    zvp_craw_build_raw_header(raw_page, sizeof(raw_page), &fhdr);
    assert(memcmp(raw_page, &fhdr, sizeof(fhdr)) == 0);
    assert(raw_page[ZVP_CRAW_FILE_HDR_BYTES - 1u] == 0u);

    char raw_p[32], wav_p[32];
    assert(zvp_craw_format_paths(3u, raw_p, sizeof(raw_p), wav_p, sizeof(wav_p)) == 0);
    assert(strcmp(raw_p, "A:/ZVC00003.RAW") == 0);
    assert(strcmp(wav_p, "A:/ZVC00003.WAV") == 0);

    /* 2. 初始化 Mock 环境 & 校验 VTable 不匹配保护 */
    memset(g_mock_craw_shm, 0, sizeof(g_mock_craw_shm));
    memset(g_mock_craw_dbg, 0, sizeof(g_mock_craw_dbg));
    memset(g_mock_craw_code, 0, sizeof(g_mock_craw_code));
    g_mock_slot_seq_movie  = 0x12345678u; /* 故意破坏原厂签名 */
    g_mock_slot_rdcw0_s7   = ZVP_AVCAM_ORIG_RDCW0_S7;
    g_mock_slot_camrdc_cfg = ZVP_AVCAM_ORIG_CAMRDC_CFG;
    g_mock_slot_camaif_s4  = ZVP_AVCAM_ORIG_CAMAIF_S4;
    g_mock_slot_ifmt_s4    = ZVP_AVCAM_ORIG_IFMT_S4;
    g_mock_slot_ifmt_s10   = ZVP_AVCAM_ORIG_IFMT_S10;
    g_mock_slot_dfe_rdc10  = ZVP_AVCAM_ORIG_DFE_RDC_S10;
    g_mock_slot_dfe_rdccfg = ZVP_AVCAM_ORIG_DFE_RDC_CFG;
    g_mock_slot_dfe_main   = ZVP_AVCAM_ORIG_DFE_MAIN;
    g_mock_slot_state_hdl  = ZVP_AVCAM_ORIG_STATE_HDL;
    g_mock_slot_dfe_rdcw0  = ZVP_AVCAM_ORIG_DFE_RDCW0;
    g_mock_ext_rdcw1_s7    = ZVP_AVCAM_EXT_ORIG_RDCW1_S7;
    g_mock_ext_rdcw1_s8    = ZVP_AVCAM_EXT_ORIG_RDCW1_S8;
    g_mock_ext_spl_ee_s2   = ZVP_AVCAM_EXT_ORIG_SPL_EE_S2;
    g_mock_ext_ee_s2       = ZVP_AVCAM_EXT_ORIG_EE_S2;
    g_mock_ext_spl_ee_s17  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S17;
    g_mock_ext_ee_s17      = ZVP_AVCAM_EXT_ORIG_EE_S17;
    g_mock_ext_spl_ee_s22  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S22;
    g_mock_ext_spl_cap_s23 = ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23;
    g_mock_dmm_held_count = 0u;
    g_mock_fsys_written_bytes = 0u;
    g_mock_fsys_wdirect_calls = 0u;

    zvp_craw_platform_ops_t ops = {
        .phys_read = mock_craw_phys_read,
        .phys_write = mock_craw_phys_write,
        .dmm_alloc = mock_craw_dmm_alloc,
        .dmm_free = mock_craw_dmm_free,
        .fsys_open_direct = mock_craw_fsys_open,
        .fsys_write_direct = mock_craw_fsys_write,
        .fsys_close = mock_craw_fsys_close,
        .fsys_write_file_direct = mock_craw_fsys_wdirect,
        .fsys_get_free_mb = mock_craw_fsys_free_mb,
        .mono_ms = mock_craw_mono_ms,
    };

    zvp_craw_ctx_t ctx;
    zvp_craw_init(&ctx, &ops);
    assert(ctx.initialized && ctx.mode == ZVP_CRAW_LIRO_BYPASS);
    assert(zvp_craw_install_liro(&ctx) == -6);
    assert(!ctx.liro_installed);

    /* 3. 恢复原厂签名后热安装 LIRO + DMM 分配 + 模式循环 + 零拷贝快照 */
    g_mock_slot_seq_movie = ZVP_AVCAM_ORIG_SEQ_MOVIE;
    assert(zvp_craw_cycle_mode(&ctx) == 0);
    assert(ctx.mode == ZVP_CRAW_LIRO_SPY_ONLY);
    assert(ctx.liro_installed);
    assert(ctx.shm_snap.hook_installed_mask == 0x7FFu);

    zvp_liro_dispatch_header_t hdr;
    memcpy(&hdr, g_mock_craw_code, sizeof(hdr));
    assert(hdr.magic == ZVP_CRAW_SHM_MAGIC);
    assert(g_mock_slot_seq_movie  == hdr.entries[ZVP_LIRO_TBL_SEQ_MOVIE]);
    assert(g_mock_slot_rdcw0_s7   == hdr.entries[ZVP_LIRO_TBL_RDCW0_S7]);
    assert(g_mock_slot_camrdc_cfg == hdr.entries[ZVP_LIRO_TBL_CAMRDC_CFG]);
    assert(g_mock_slot_camaif_s4  == hdr.entries[ZVP_LIRO_TBL_CAMAIF_S4]);
    assert(g_mock_slot_ifmt_s4    == hdr.entries[ZVP_LIRO_TBL_IFMT_S4]);
    assert(g_mock_slot_ifmt_s10   == hdr.entries[ZVP_LIRO_TBL_IFMT_S10]);
    assert(g_mock_slot_dfe_rdc10  == hdr.entries[ZVP_LIRO_TBL_DFE_RDC_S10]);
    assert(g_mock_slot_dfe_rdccfg == hdr.entries[ZVP_LIRO_TBL_DFE_RDC_CFG]);
    assert(g_mock_slot_dfe_main   == hdr.entries[ZVP_LIRO_TBL_DFE_MAIN]);
    assert(g_mock_slot_state_hdl  == hdr.entries[ZVP_LIRO_TBL_STATE_HDL]);
    assert(g_mock_slot_dfe_rdcw0  == hdr.entries[ZVP_LIRO_TBL_DFE_RDCW0]);
    assert(g_mock_ext_rdcw1_s7    == hdr.ext_entries[ZVP_LIRO_EXT_RDCW1_S7]);
    assert(g_mock_ext_rdcw1_s8    == hdr.ext_entries[ZVP_LIRO_EXT_RDCW1_S8]);
    assert(g_mock_ext_spl_ee_s2   == hdr.ext_entries[ZVP_LIRO_EXT_SPL_EE_S2]);
    assert(g_mock_ext_ee_s2       == hdr.ext_entries[ZVP_LIRO_EXT_EE_S2]);
    assert(g_mock_ext_spl_ee_s17  == hdr.ext_entries[ZVP_LIRO_EXT_SPL_EE_S17]);
    assert(g_mock_ext_ee_s17      == hdr.ext_entries[ZVP_LIRO_EXT_EE_S17]);
    assert(g_mock_ext_spl_ee_s22  == hdr.ext_entries[ZVP_LIRO_EXT_SPL_EE_S22]);
    assert(g_mock_ext_spl_cap_s23 == hdr.ext_entries[ZVP_LIRO_EXT_SPL_CAP_S23]);
    zvp_craw_dbg_t live_dbg;
    memcpy(&live_dbg, g_mock_craw_dbg, sizeof(live_dbg));
    assert(live_dbg.v18_ext_installed_mask == 0xFFu);

    assert(zvp_craw_cycle_mode(&ctx) == 0);
    assert(ctx.mode == ZVP_CRAW_LIRO_ARM_2K);
    assert(ctx.pool_allocated && ctx.block_count == 7u);
    assert(ctx.pool_total_bytes == 40370176u);
    assert(ctx.total_slots == 14u);
    assert(g_mock_dmm_held_count == 7u);

    zvp_craw_shm_t live_shm;
    memcpy(&live_shm, g_mock_craw_shm, sizeof(live_shm));
    assert(live_shm.mode == ZVP_CRAW_LIRO_ARM_2K);
    assert(live_shm.ring_base_pa == 0x1A000000u);
    assert(live_shm.slot_count == 14u);

    assert(zvp_craw_dump_test_snapshot(&ctx) == 0);
    assert(g_mock_fsys_written_bytes == 8u * 1048576u);
    assert(g_mock_fsys_wdirect_calls == 1u);
    assert(ctx.clip_seq == 2u && ctx.free_sd_mb == 4096u);
    assert(memcmp(g_mock_craw_hdr_page, &fhdr, sizeof(uint32_t) * 6u) == 0);

    /* 3b. 模式循环继续进入 ZVP_CRAW_LIRO_RAW_W0 与归零回到 BYPASS */
    assert(zvp_craw_cycle_mode(&ctx) == 0);
    assert(ctx.mode == ZVP_CRAW_LIRO_RAW_W0);
    assert(ctx.frame_bytes == 4608u * 1024u);
    memcpy(&live_shm, g_mock_craw_shm, sizeof(live_shm));
    assert(live_shm.mode == ZVP_CRAW_LIRO_RAW_W0);
    assert(live_shm.frame_stride == 4608u * 1024u);
    assert(zvp_craw_cycle_mode(&ctx) == 0);
    assert(ctx.mode == ZVP_CRAW_LIRO_BYPASS);

    /* 4. 卸载恢复全部 VTable 与释放全部 DMM 句柄 */
    zvp_craw_shutdown(&ctx);
    assert(!ctx.liro_installed && !ctx.pool_allocated);
    assert(ctx.mode == ZVP_CRAW_LIRO_BYPASS);
    assert(g_mock_dmm_held_count == 0u);
    assert(g_mock_slot_seq_movie  == ZVP_AVCAM_ORIG_SEQ_MOVIE);
    assert(g_mock_slot_rdcw0_s7   == ZVP_AVCAM_ORIG_RDCW0_S7);
    assert(g_mock_slot_camrdc_cfg == ZVP_AVCAM_ORIG_CAMRDC_CFG);
    assert(g_mock_slot_camaif_s4  == ZVP_AVCAM_ORIG_CAMAIF_S4);
    assert(g_mock_slot_ifmt_s4    == ZVP_AVCAM_ORIG_IFMT_S4);
    assert(g_mock_slot_ifmt_s10   == ZVP_AVCAM_ORIG_IFMT_S10);
    assert(g_mock_slot_dfe_rdc10  == ZVP_AVCAM_ORIG_DFE_RDC_S10);
    assert(g_mock_slot_dfe_rdccfg == ZVP_AVCAM_ORIG_DFE_RDC_CFG);
    assert(g_mock_slot_dfe_main   == ZVP_AVCAM_ORIG_DFE_MAIN);
    assert(g_mock_slot_state_hdl  == ZVP_AVCAM_ORIG_STATE_HDL);
    assert(g_mock_slot_dfe_rdcw0  == ZVP_AVCAM_ORIG_DFE_RDCW0);
    assert(g_mock_ext_rdcw1_s7    == ZVP_AVCAM_EXT_ORIG_RDCW1_S7);
    assert(g_mock_ext_rdcw1_s8    == ZVP_AVCAM_EXT_ORIG_RDCW1_S8);
    assert(g_mock_ext_spl_ee_s2   == ZVP_AVCAM_EXT_ORIG_SPL_EE_S2);
    assert(g_mock_ext_ee_s2       == ZVP_AVCAM_EXT_ORIG_EE_S2);
    assert(g_mock_ext_spl_ee_s17  == ZVP_AVCAM_EXT_ORIG_SPL_EE_S17);
    assert(g_mock_ext_ee_s17      == ZVP_AVCAM_EXT_ORIG_EE_S17);
    assert(g_mock_ext_spl_ee_s22  == ZVP_AVCAM_EXT_ORIG_SPL_EE_S22);
    assert(g_mock_ext_spl_cap_s23 == ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23);
    memcpy(&live_dbg, g_mock_craw_dbg, sizeof(live_dbg));
    assert(live_dbg.v18_ext_installed_mask == 0u);

    /* 5. 回归验证：热重启后 SHM 保留 "ZVCR" 魔数与衰减噪声，同时代码区被损坏时必须强刷重写 */
    zvp_craw_shm_t decayed_shm;
    memset(&decayed_shm, 0xA5, sizeof(decayed_shm));
    decayed_shm.magic = ZVP_CRAW_SHM_MAGIC;
    decayed_shm.orig_seq_movie = 0xDEADBEEFu;
    memcpy(g_mock_craw_shm, &decayed_shm, sizeof(decayed_shm));
    g_mock_craw_code[0] = 0x76u;
    g_mock_craw_code[1] = 0x47u;
    g_mock_craw_code[2] = 0x56u;
    g_mock_craw_code[3] = 0x5Eu; /* 损坏为 0x5E564776 */

    zvp_craw_ctx_t ctx2;
    zvp_craw_init(&ctx2, &ops);
    assert(zvp_craw_set_mode(&ctx2, ZVP_CRAW_LIRO_SPY_ONLY) == 0);
    assert(ctx2.liro_installed);
    assert(memcmp(g_mock_craw_code, zvp_liro_blob_bytes, ZVP_LIRO_BLOB_SIZE) == 0);
    assert(ctx2.shm_snap.orig_seq_movie == ZVP_AVCAM_ORIG_SEQ_MOVIE);
    assert(ctx2.shm_snap.seq_calls == 0u && ctx2.shm_snap.write_seq == 0u);
    zvp_craw_shutdown(&ctx2);
}

/* ============================================================================
 * 2K cRAW 连续录像排空状态机 (视频环跨尾 + 48kHz LPCM 环回绕 + Seek定稿) 单测
 * ========================================================================== */

#define MOCK_V2_RING_SLOTS   4u
#define MOCK_V2_STRIDE       4096u
#define MOCK_V2_RING_PA      0x1B000000u
#define MOCK_V2_STAGE_PA     0x3E700000u
#define MOCK_V2_STAGE_SIZE   65536u
#define MOCK_V2_ADF_PA       0x041FDD34u
#define MOCK_V2_LPCM_PA      0x3E842400u
#define MOCK_V2_LPCM_SIZE    16384u

static uint8_t  g_v2_ring_mem[MOCK_V2_RING_SLOTS * MOCK_V2_STRIDE];
static uint8_t  g_v2_stage_mem[MOCK_V2_STAGE_SIZE];
static uint8_t  g_v2_adf_mem[0x60];
static uint8_t  g_v2_lpcm_mem[MOCK_V2_LPCM_SIZE];
static uint8_t  g_v2_raw_disk[131072];
static uint8_t  g_v2_wav_disk[65536];
static uint32_t g_v2_raw_pos = 0u;
static uint32_t g_v2_raw_len = 0u;
static uint32_t g_v2_wav_pos = 0u;
static uint32_t g_v2_wav_len = 0u;
static uint32_t g_v2_wrap_ext2_seen = 0u;

static void *v2_map_pa(uint32_t pa, size_t len, size_t *avail)
{
    if (pa >= MOCK_V2_RING_PA && (pa + len) <= (MOCK_V2_RING_PA + sizeof(g_v2_ring_mem))) {
        *avail = sizeof(g_v2_ring_mem) - (pa - MOCK_V2_RING_PA);
        return &g_v2_ring_mem[pa - MOCK_V2_RING_PA];
    }
    if (pa >= MOCK_V2_STAGE_PA && (pa + len) <= (MOCK_V2_STAGE_PA + sizeof(g_v2_stage_mem))) {
        *avail = sizeof(g_v2_stage_mem) - (pa - MOCK_V2_STAGE_PA);
        return &g_v2_stage_mem[pa - MOCK_V2_STAGE_PA];
    }
    if (pa >= MOCK_V2_ADF_PA && (pa + len) <= (MOCK_V2_ADF_PA + sizeof(g_v2_adf_mem))) {
        *avail = sizeof(g_v2_adf_mem) - (pa - MOCK_V2_ADF_PA);
        return &g_v2_adf_mem[pa - MOCK_V2_ADF_PA];
    }
    if (pa >= MOCK_V2_LPCM_PA && (pa + len) <= (MOCK_V2_LPCM_PA + sizeof(g_v2_lpcm_mem))) {
        *avail = sizeof(g_v2_lpcm_mem) - (pa - MOCK_V2_LPCM_PA);
        return &g_v2_lpcm_mem[pa - MOCK_V2_LPCM_PA];
    }
    return mock_craw_map_pa(pa, len, avail);
}

static int v2_phys_read(uint32_t pa, void *out_buf, size_t len)
{
    size_t avail = 0u;
    void *p = v2_map_pa(pa, len, &avail);
    if (!p || avail < len) return -1;
    memcpy(out_buf, p, len);
    return 0;
}

static int v2_phys_write(uint32_t pa, const void *buf, size_t len)
{
    size_t avail = 0u;
    void *p = v2_map_pa(pa, len, &avail);
    if (!p || avail < len) return -1;
    memcpy(p, buf, len);
    return 0;
}

static int v2_dmm_alloc(uint32_t unit_type, uint16_t *out_uhdl, uint32_t *out_pa, uint32_t *out_sz)
{
    if (unit_type == 0x0003c2u) {
        *out_uhdl = 200u; *out_pa = MOCK_V2_RING_PA; *out_sz = sizeof(g_v2_ring_mem);
        return 0;
    }
    if (unit_type == 0x081227u) {
        *out_uhdl = 201u; *out_pa = MOCK_V2_STAGE_PA; *out_sz = sizeof(g_v2_stage_mem);
        return 0;
    }
    return -2;
}

static int v2_dmm_free(uint16_t uhdl) { (void)uhdl; return 0; }

static int v2_fsys_open(const char *path, int *out_fh)
{
    if (strstr(path, ".RAW")) { *out_fh = 101; return 0; }
    if (strstr(path, ".WAV")) { *out_fh = 102; return 0; }
    return -1;
}

static int v2_fsys_seek(int fh, uint64_t offset, int whence)
{
    assert(whence == 0);
    if (fh == 101) { g_v2_raw_pos = (uint32_t)offset; return 0; }
    if (fh == 102) { g_v2_wav_pos = (uint32_t)offset; return 0; }
    return -1;
}

static int v2_fsys_write(int fh, uint32_t align_sz,
                         const zvp_craw_extent_t *exts, uint32_t count,
                         uint32_t total_bytes)
{
    assert(align_sz == ZVP_CRAW_PAGE_ALIGN);
    assert((total_bytes % ZVP_CRAW_PAGE_ALIGN) == 0u);
    if (count == 2u && fh == 101) g_v2_wrap_ext2_seen++;

    uint8_t *dst = (fh == 101) ? g_v2_raw_disk : g_v2_wav_disk;
    uint32_t *pos = (fh == 101) ? &g_v2_raw_pos : &g_v2_wav_pos;
    uint32_t *len = (fh == 101) ? &g_v2_raw_len : &g_v2_wav_len;
    uint32_t cap = (fh == 101) ? (uint32_t)sizeof(g_v2_raw_disk) : (uint32_t)sizeof(g_v2_wav_disk);

    uint32_t sum = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        assert((*pos + exts[i].size) <= cap);
        assert(v2_phys_read(exts[i].paddr, dst + *pos, exts[i].size) == 0);
        *pos += exts[i].size;
        if (*pos > *len) *len = *pos;
        sum += exts[i].size;
    }
    assert(sum == total_bytes);
    return (int)total_bytes;
}

static int v2_fsys_close(int fh)
{
    assert(fh == 101 || fh == 102);
    return 0;
}

static void test_craw_drainer_and_audio_ring(void)
{
    memset(g_mock_craw_shm, 0, sizeof(g_mock_craw_shm));
    memset(g_mock_craw_code, 0, sizeof(g_mock_craw_code));
    memset(g_v2_ring_mem, 0, sizeof(g_v2_ring_mem));
    memset(g_v2_stage_mem, 0, sizeof(g_v2_stage_mem));
    memset(g_v2_adf_mem, 0, sizeof(g_v2_adf_mem));
    memset(g_v2_lpcm_mem, 0, sizeof(g_v2_lpcm_mem));
    memset(g_v2_raw_disk, 0, sizeof(g_v2_raw_disk));
    memset(g_v2_wav_disk, 0, sizeof(g_v2_wav_disk));
    g_v2_raw_pos = 0u; g_v2_raw_len = 0u;
    g_v2_wav_pos = 0u; g_v2_wav_len = 0u;
    g_v2_wrap_ext2_seen = 0u;

    g_mock_slot_seq_movie  = ZVP_AVCAM_ORIG_SEQ_MOVIE;
    g_mock_slot_rdcw0_s7   = ZVP_AVCAM_ORIG_RDCW0_S7;
    g_mock_slot_camrdc_cfg = ZVP_AVCAM_ORIG_CAMRDC_CFG;
    g_mock_slot_camaif_s4  = ZVP_AVCAM_ORIG_CAMAIF_S4;
    g_mock_slot_ifmt_s4    = ZVP_AVCAM_ORIG_IFMT_S4;
    g_mock_slot_ifmt_s10   = ZVP_AVCAM_ORIG_IFMT_S10;
    g_mock_slot_dfe_rdc10  = ZVP_AVCAM_ORIG_DFE_RDC_S10;
    g_mock_slot_dfe_rdccfg = ZVP_AVCAM_ORIG_DFE_RDC_CFG;
    g_mock_slot_dfe_main   = ZVP_AVCAM_ORIG_DFE_MAIN;
    g_mock_slot_state_hdl  = ZVP_AVCAM_ORIG_STATE_HDL;
    g_mock_slot_dfe_rdcw0  = ZVP_AVCAM_ORIG_DFE_RDCW0;
    g_mock_ext_rdcw1_s7    = ZVP_AVCAM_EXT_ORIG_RDCW1_S7;
    g_mock_ext_rdcw1_s8    = ZVP_AVCAM_EXT_ORIG_RDCW1_S8;
    g_mock_ext_spl_ee_s2   = ZVP_AVCAM_EXT_ORIG_SPL_EE_S2;
    g_mock_ext_ee_s2       = ZVP_AVCAM_EXT_ORIG_EE_S2;
    g_mock_ext_spl_ee_s17  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S17;
    g_mock_ext_ee_s17      = ZVP_AVCAM_EXT_ORIG_EE_S17;
    g_mock_ext_spl_ee_s22  = ZVP_AVCAM_EXT_ORIG_SPL_EE_S22;
    g_mock_ext_spl_cap_s23 = ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23;

    zvp_craw_platform_ops_t ops = {
        .phys_read = v2_phys_read,
        .phys_write = v2_phys_write,
        .dmm_alloc = v2_dmm_alloc,
        .dmm_free = v2_dmm_free,
        .fsys_open_direct = v2_fsys_open,
        .fsys_write_direct = v2_fsys_write,
        .fsys_close = v2_fsys_close,
        .fsys_seek = v2_fsys_seek,
        .fsys_get_free_mb = mock_craw_fsys_free_mb,
        .mono_ms = mock_craw_mono_ms,
    };

    zvp_craw_ctx_t ctx;
    zvp_craw_init(&ctx, &ops);
    /* 用 4KB 单帧步长测试 4 槽环绕 */
    ctx.frame_bytes = MOCK_V2_STRIDE;
    assert(zvp_craw_set_mode(&ctx, ZVP_CRAW_LIRO_ARM_2K) == 0);
    assert(ctx.total_slots == MOCK_V2_RING_SLOTS);

    /* 准备 LPCM 环形内容：每字节 (idx & 0x7F) | 0x80，便于逐字节验真 */
    for (uint32_t i = 0; i < MOCK_V2_LPCM_SIZE; ++i) {
        g_v2_lpcm_mem[i] = (uint8_t)((i & 0x7Fu) | 0x80u);
    }
    uint32_t *adf = (uint32_t *)(void *)g_v2_adf_mem;
    adf[0x3Cu / 4u] = MOCK_V2_LPCM_PA;
    adf[0x40u / 4u] = MOCK_V2_LPCM_SIZE;
    adf[0x44u / 4u] = MOCK_V2_LPCM_PA + 12288u; /* 起始写指针在环尾部前方 */
    adf[0x48u / 4u] = 6144u;

    /* 模拟 0x0108 开录，写入前 3 帧 (slot 0,1,2) */
    zvp_craw_shm_t *shm = (zvp_craw_shm_t *)(void *)g_mock_craw_shm;
    shm->rec_state = 1u;
    shm->audio_ptr = MOCK_V2_LPCM_PA + 12288u;
    for (uint32_t f = 0; f < 3u; ++f) {
        memset(&g_v2_ring_mem[f * MOCK_V2_STRIDE], (int)(0xA0u + f), MOCK_V2_STRIDE);
    }
    shm->write_seq = 3u;
    /* 音频指针推进 +6144 -> 回绕至 LPCM_PA + 2048u */
    adf[0x44u / 4u] = MOCK_V2_LPCM_PA + 2048u;

    assert(zvp_craw_drainer_step(&ctx) == 0);
    assert(ctx.rec_clip_open);
    assert(ctx.clip_frames == 3u && shm->read_seq == 3u);
    assert(ctx.clip_audio_bytes == 6144u);

    /* 再写入第 3, 4, 5 帧 (slot 3 -> wrap to 0, 1)，一次跨尾排空 3 帧触发 ext_cnt==2 */
    for (uint32_t seq = 3u; seq < 6u; ++seq) {
        uint32_t slot = seq % MOCK_V2_RING_SLOTS;
        memset(&g_v2_ring_mem[slot * MOCK_V2_STRIDE], (int)(0xA0u + seq), MOCK_V2_STRIDE);
    }
    shm->write_seq = 6u;
    shm->drop_seq = 2u;
    /* 音频再推进 4096 B 至 LPCM_PA + 6144u (总音长 10240 B) */
    adf[0x44u / 4u] = MOCK_V2_LPCM_PA + 6144u;
    assert(zvp_craw_drainer_step(&ctx) == 0);
    assert(g_v2_wrap_ext2_seen == 1u);
    assert(ctx.clip_frames == 6u && shm->read_seq == 6u);
    assert(ctx.clip_audio_bytes == 10240u);

    /* 模拟 0x0109 停录，定稿 RAW & WAV 头 */
    shm->rec_state = 0u;
    assert(zvp_craw_drainer_step(&ctx) == 0);
    assert(!ctx.rec_clip_open);
    assert(ctx.clip_seq == 2u);

    /* 校验 RAW 文件头与 6 帧载荷 */
    zvp_craw_file_hdr_t out_fhdr;
    memcpy(&out_fhdr, g_v2_raw_disk, sizeof(out_fhdr));
    assert(out_fhdr.magic == ZVP_CRAW_FILE_HDR_MAGIC);
    assert(out_fhdr.frame_count == 6u);
    assert(out_fhdr.drop_count == 2u);
    assert(out_fhdr.frame_bytes == MOCK_V2_STRIDE);
    for (uint32_t f = 0; f < 6u; ++f) {
        uint32_t off = ZVP_CRAW_FILE_HDR_BYTES + f * MOCK_V2_STRIDE;
        assert(g_v2_raw_disk[off] == (uint8_t)(0xA0u + f));
        assert(g_v2_raw_disk[off + MOCK_V2_STRIDE - 1u] == (uint8_t)(0xA0u + f));
    }

    /* 校验 WAV 最终 44B 头及 offset 44 起全部 10240 B (跨 Seek(0) 覆盖后前导 4052 B 无损!) */
    uint32_t wav_riff_sz = 0u, wav_data_sz = 0u;
    memcpy(&wav_riff_sz, g_v2_wav_disk + 4, 4);
    memcpy(&wav_data_sz, g_v2_wav_disk + 40, 4);
    assert(memcmp(g_v2_wav_disk, "RIFF", 4) == 0);
    assert(wav_data_sz == 10240u);
    assert(wav_riff_sz == 36u + 10240u);
    for (uint32_t i = 0; i < 10240u; ++i) {
        uint32_t ring_off = (12288u + i) % MOCK_V2_LPCM_SIZE;
        uint8_t expect_b = (uint8_t)((ring_off & 0x7Fu) | 0x80u);
        assert(g_v2_wav_disk[44u + i] == expect_b);
    }

    zvp_craw_shutdown(&ctx);

    /* 校验 ZVP_CRAW_LIRO_RAW_W0 (16-bit uncompressed Bayer) 与 8B 对齐宽 (1161->1164) 头部透传 */
    g_mock_slot_seq_movie = ZVP_AVCAM_ORIG_SEQ_MOVIE;
    memset(g_mock_craw_shm, 0, sizeof(g_mock_craw_shm));
    memset(g_v2_raw_disk, 0, sizeof(g_v2_raw_disk));
    zvp_craw_init(&ctx, &ops);
    ctx.frame_bytes = MOCK_V2_STRIDE;
    assert(zvp_craw_set_mode(&ctx, ZVP_CRAW_LIRO_RAW_W0) == 0);
    assert(ctx.mode == ZVP_CRAW_LIRO_RAW_W0);
    /* 验证 target_enc_type=1 -> bpp=16 元数据透传与 16-bit RAW 头写出 */
    shm = (zvp_craw_shm_t *)(void *)g_mock_craw_shm;
    shm->frame_stride = MOCK_V2_STRIDE;
    shm->slot_count = MOCK_V2_RING_SLOTS;
    shm->target_w = (((1161u * 2u) + 7u) & ~7u) >> 1;
    shm->target_h = 504u;
    shm->target_enc_type = 1u;
    shm->rec_state = 1u;
    shm->write_seq = 1u;
    memset(&g_v2_ring_mem[0], 0x5Au, MOCK_V2_STRIDE);
    assert(zvp_craw_drainer_step(&ctx) == 0);
    assert(ctx.clip_frames == 1u);
    shm->rec_state = 0u;
    assert(zvp_craw_drainer_step(&ctx) == 0);
    memcpy(&out_fhdr, g_v2_raw_disk, sizeof(out_fhdr));
    assert(out_fhdr.width == 1164u && out_fhdr.height == 504u);
    assert(out_fhdr.bpp == 16u && out_fhdr.enc_type == 1u);
    zvp_craw_shutdown(&ctx);
}

/* ============================================================================
 * Custom LUT (zvc_fl4.ko + ZVLOOK01 + zvc.cfg) 阶段1 核心逻辑单元测试
 * ========================================================================== */

static int      g_mock_lut_kmod_calls = 0;
static int      g_mock_lut_kmod_ret = 0;
static bool     g_mock_lut_kmod_loaded = false;
static uint32_t g_mock_lut_digest = 0u;
static uint32_t g_mock_lut_expect_count = 0u;
static uint32_t g_mock_lut_loaded_slots = 0u;
static uint32_t g_mock_lut_last_apply_bits = 0u;
static uint32_t g_mock_lut_last_apply_sel = 0u;
static int      g_mock_lut_refresh_calls = 0;
static int32_t  g_mock_lut_last_sharp = 0;
static bool     g_mock_lut_hot_staging = false;
static bool     g_mock_lut_hot_prepared = false;
static uint32_t g_mock_lut_hot_sel = 0u;
static uint32_t g_mock_lut_hot_bits = 0u;
static int      g_mock_lut_hot_commits = 0;
static int      g_mock_lut_hot_aborts = 0;
static long     g_mock_lut_force_hot_err = 0L;

static uint8_t  g_mock_lut_bundle_disk[ZVP_LUT_BUNDLE_MAX_BYTES];
static size_t   g_mock_lut_bundle_disk_len = 0u;
static uint8_t  g_mock_lut_sd_disk[ZVP_LUT_BUNDLE_MAX_BYTES];
static size_t   g_mock_lut_sd_disk_len = 0u;
static uint8_t  g_mock_lut_cfg_disk[1024];
static size_t   g_mock_lut_cfg_disk_len = 0u;

static int mock_lut_init_module(const void *ko_data, size_t ko_len, const char *opts)
{
    (void)opts;
    assert(ko_data != NULL && ko_len == ZVP_LUT_G_ZVP_FL4_KO_BLOB_SIZE);
    g_mock_lut_kmod_calls++;
    if (g_mock_lut_kmod_ret != 0) {
        return g_mock_lut_kmod_ret;
    }
    if (g_mock_lut_kmod_loaded) {
        return -17; /* -EEXIST */
    }
    g_mock_lut_kmod_loaded = true;
    return 0;
}

static long mock_lut_sys_write(uint32_t fd, uintptr_t arg1, uintptr_t arg2)
{
    if (!g_mock_lut_kmod_loaded) {
        return -9; /* -EBADF */
    }
    if (fd == ZVP_LUT_FD_CATALOG) {
        uint32_t cmd = (uint32_t)arg2;
        if (cmd == ZVP_LUT_CAT_MATCH) {
            return (g_mock_lut_digest != 0u && g_mock_lut_digest == (uint32_t)arg1) ? 0L : -2L;
        }
        if (cmd >= 1u && cmd <= ZVP_LUT_VISIBLE_LOOKS) {
            g_mock_lut_expect_count = cmd;
            g_mock_lut_loaded_slots = 0u;
            return 0L;
        }
        if (cmd == ZVP_LUT_CAT_COMMIT) {
            if (g_mock_lut_loaded_slots != g_mock_lut_expect_count) {
                return -22L;
            }
            g_mock_lut_digest = (uint32_t)arg1;
            return 0L;
        }
        if (cmd == ZVP_LUT_CAT_ABORT) {
            g_mock_lut_expect_count = 0u;
            g_mock_lut_loaded_slots = 0u;
            return 0L;
        }
        if (cmd >= (ZVP_LUT_CAT_HOT_BEGIN + 1u) && cmd <= (ZVP_LUT_CAT_HOT_BEGIN + ZVP_LUT_VISIBLE_LOOKS)) {
            if (g_mock_lut_hot_staging || g_mock_lut_digest == 0u || (uint32_t)arg1 != g_mock_lut_digest) {
                return -22L;
            }
            g_mock_lut_expect_count = cmd - ZVP_LUT_CAT_HOT_BEGIN;
            g_mock_lut_loaded_slots = 0u;
            g_mock_lut_hot_staging = true;
            g_mock_lut_hot_prepared = false;
            return 0L;
        }
        if (cmd == ZVP_LUT_CAT_HOT_ABORT) {
            g_mock_lut_expect_count = 0u;
            g_mock_lut_loaded_slots = 0u;
            g_mock_lut_hot_staging = false;
            g_mock_lut_hot_prepared = false;
            g_mock_lut_hot_aborts++;
            return 0L;
        }
        if (cmd >= ZVP_LUT_CAT_HOT_SELECT && cmd <= (ZVP_LUT_CAT_HOT_SELECT + ZVP_LUT_VISIBLE_LOOKS)) {
            if (!g_mock_lut_hot_staging || g_mock_lut_hot_prepared ||
                g_mock_lut_loaded_slots != g_mock_lut_expect_count ||
                (cmd - ZVP_LUT_CAT_HOT_SELECT) > g_mock_lut_expect_count ||
                !zvp_lut_adjust_valid(zvp_lut_unpack_adjust((uint32_t)arg1))) {
                return -22L;
            }
            g_mock_lut_hot_sel = cmd - ZVP_LUT_CAT_HOT_SELECT;
            g_mock_lut_hot_bits = (uint32_t)arg1;
            g_mock_lut_hot_prepared = true;
            return 0L;
        }
        if (cmd == ZVP_LUT_CAT_HOT_COMMIT) {
            if (g_mock_lut_force_hot_err != 0L) {
                return g_mock_lut_force_hot_err;
            }
            if (!g_mock_lut_hot_staging || !g_mock_lut_hot_prepared) {
                return -22L;
            }
            g_mock_lut_digest = (uint32_t)arg1;
            g_mock_lut_last_apply_sel = g_mock_lut_hot_sel;
            g_mock_lut_last_apply_bits = g_mock_lut_hot_bits;
            g_mock_lut_expect_count = 0u;
            g_mock_lut_loaded_slots = 0u;
            g_mock_lut_hot_staging = false;
            g_mock_lut_hot_prepared = false;
            g_mock_lut_hot_commits++;
            return 0L;
        }
        return -22L;
    }
    if (fd == ZVP_LUT_FD_LOAD) {
        const zvp_lut_slot_packet_t *pkt = (const zvp_lut_slot_packet_t *)arg1;
        assert(arg2 == sizeof(*pkt));
        assert(pkt != NULL && pkt->slot_idx == g_mock_lut_loaded_slots);
        g_mock_lut_loaded_slots++;
        return 0L;
    }
    if (fd == ZVP_LUT_FD_APPLY) {
        g_mock_lut_last_apply_bits = (uint32_t)arg1;
        g_mock_lut_last_apply_sel = (uint32_t)arg2;
        return 0L;
    }
    return -9L;
}

static int mock_lut_read_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (strcmp(path, ZVP_LUT_BUNDLE_PATH) == 0 && g_mock_lut_bundle_disk_len > 0u) {
        if (g_mock_lut_bundle_disk_len > cap) return -27;
        memcpy(buf, g_mock_lut_bundle_disk, g_mock_lut_bundle_disk_len);
        if (out_len) *out_len = g_mock_lut_bundle_disk_len;
        return 0;
    }
    if (strcmp(path, ZVP_LUT_CFG_PATH) == 0 && g_mock_lut_cfg_disk_len > 0u) {
        if (g_mock_lut_cfg_disk_len > cap) return -27;
        memcpy(buf, g_mock_lut_cfg_disk, g_mock_lut_cfg_disk_len);
        if (out_len) *out_len = g_mock_lut_cfg_disk_len;
        return 0;
    }
    return -2;
}

static int mock_lut_atomic_write(const char *dir, const char *path, const void *buf, size_t len)
{
    assert(strcmp(dir, ZVP_LUT_DIR_PATH) == 0);
    if (strcmp(path, ZVP_LUT_BUNDLE_PATH) == 0) {
        assert(len <= sizeof(g_mock_lut_bundle_disk));
        memcpy(g_mock_lut_bundle_disk, buf, len);
        g_mock_lut_bundle_disk_len = len;
        return 0;
    }
    if (strcmp(path, ZVP_LUT_CFG_PATH) == 0) {
        assert(len <= sizeof(g_mock_lut_cfg_disk));
        memcpy(g_mock_lut_cfg_disk, buf, len);
        g_mock_lut_cfg_disk_len = len;
        return 0;
    }
    return -22;
}

static int mock_lut_refresh_preview(int8_t req_sharpness,
                                    int8_t *io_native_sharpness,
                                    zvp_lut_preview_info_t *out_info)
{
    g_mock_lut_refresh_calls++;
    if (io_native_sharpness && *io_native_sharpness == ZVP_LUT_SHARPNESS_NATIVE) {
        *io_native_sharpness = 0;
    }
    g_mock_lut_last_sharp = (req_sharpness == ZVP_LUT_SHARPNESS_NATIVE) ? 0 : req_sharpness;
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
        out_info->rc = 0;
        out_info->style = 0;
        out_info->stock_sharpness = 0;
        out_info->applied_sharpness = g_mock_lut_last_sharp;
    }
    return 0;
}

static int mock_lut_scan_sd(zvp_lut_sd_item_t *out_items, uint32_t max_items, uint32_t *out_count)
{
    if (out_count) *out_count = 0u;
    if (!out_items || max_items == 0u || g_mock_lut_sd_disk_len == 0u) return 0;
    uint32_t lcnt = 0u;
    if (zvp_lut_validate_bundle(g_mock_lut_sd_disk, g_mock_lut_sd_disk_len, &lcnt, NULL) != 0) return 0;
    snprintf(out_items[0].name, sizeof(out_items[0].name), "custom_pack.bin");
    snprintf(out_items[0].path, sizeof(out_items[0].path), "B:/ZVC/custom_pack.bin");
    out_items[0].size = (uint32_t)g_mock_lut_sd_disk_len;
    out_items[0].look_count = (uint8_t)lcnt;
    out_items[0].is_embedded = false;
    if (out_count) *out_count = 1u;
    return 0;
}

static int mock_lut_read_sd(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (strcmp(path, "B:/ZVC/custom_pack.bin") == 0 && g_mock_lut_sd_disk_len > 0u) {
        if (g_mock_lut_sd_disk_len > cap) return -27;
        memcpy(buf, g_mock_lut_sd_disk, g_mock_lut_sd_disk_len);
        if (out_len) *out_len = g_mock_lut_sd_disk_len;
        return 0;
    }
    return -2;
}

static void wr_le32_test(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static size_t build_test_sd_bundle(uint8_t *out_buf)
{
    const uint32_t count = 3u;
    size_t total = ZVP_LUT_BUNDLE_HEADER + count * ZVP_LUT_BUNDLE_ENTRY + 4u;
    memcpy(out_buf, g_zvp_lut_default_bundle_blob, ZVP_LUT_BUNDLE_HEADER);
    wr_le32_test(out_buf + 12u, count);

    /* 第 1 槽：自定义滤镜 "custom-2383" / "Kodak2383"；第 2 槽保留 "fuji-nc" 测试已有微调留存；第 3 槽复制自第 1 槽 */
    uint8_t *e0 = out_buf + ZVP_LUT_BUNDLE_HEADER;
    uint8_t *e1 = e0 + ZVP_LUT_BUNDLE_ENTRY;
    uint8_t *e2 = e1 + ZVP_LUT_BUNDLE_ENTRY;
    memcpy(e0, g_zvp_lut_default_bundle_blob + ZVP_LUT_BUNDLE_HEADER + 0u * ZVP_LUT_BUNDLE_ENTRY, ZVP_LUT_BUNDLE_ENTRY);
    memcpy(e1, g_zvp_lut_default_bundle_blob + ZVP_LUT_BUNDLE_HEADER + 9u * ZVP_LUT_BUNDLE_ENTRY, ZVP_LUT_BUNDLE_ENTRY); /* fuji-nc */
    memcpy(e2, g_zvp_lut_default_bundle_blob + ZVP_LUT_BUNDLE_HEADER + 16u * ZVP_LUT_BUNDLE_ENTRY, ZVP_LUT_BUNDLE_ENTRY);

    memset(e0, 0, 96u);
    snprintf((char *)e0, 32u, "custom-2383");
    snprintf((char *)(e0 + 32u), 64u, "Kodak2383");
    wr_le32_test(e0 + ZVP_LUT_BUNDLE_ENTRY - 4u, zvp_lut_crc32(e0, ZVP_LUT_BUNDLE_ENTRY - 4u));
    wr_le32_test(e1 + ZVP_LUT_BUNDLE_ENTRY - 4u, zvp_lut_crc32(e1, ZVP_LUT_BUNDLE_ENTRY - 4u));
    wr_le32_test(e2 + ZVP_LUT_BUNDLE_ENTRY - 4u, zvp_lut_crc32(e2, ZVP_LUT_BUNDLE_ENTRY - 4u));
    wr_le32_test(out_buf + total - 4u, zvp_lut_crc32(out_buf, total - 4u));
    return total;
}

static void test_lut_core_and_bundle(void)
{
    g_mock_lut_kmod_calls = 0;
    g_mock_lut_kmod_ret = 0;
    g_mock_lut_kmod_loaded = false;
    g_mock_lut_digest = 0u;
    g_mock_lut_expect_count = 0u;
    g_mock_lut_loaded_slots = 0u;
    g_mock_lut_last_apply_bits = 0u;
    g_mock_lut_last_apply_sel = 99u;
    g_mock_lut_refresh_calls = 0;
    g_mock_lut_hot_staging = false;
    g_mock_lut_hot_prepared = false;
    g_mock_lut_hot_commits = 0;
    g_mock_lut_hot_aborts = 0;
    g_mock_lut_force_hot_err = 0L;
    g_mock_lut_bundle_disk_len = 0u;
    g_mock_lut_sd_disk_len = 0u;
    g_mock_lut_cfg_disk_len = 0u;

    zvp_lut_platform_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.init_module         = mock_lut_init_module;
    ops.sys_write_fd        = mock_lut_sys_write;
    ops.read_file           = mock_lut_read_file;
    ops.atomic_write_file   = mock_lut_atomic_write;
    ops.refresh_preview     = mock_lut_refresh_preview;
    ops.scan_sd_bundles     = mock_lut_scan_sd;
    ops.read_sd_file        = mock_lut_read_sd;
    ops.default_ko_data     = g_zvp_fl4_ko_blob;
    ops.default_ko_len      = ZVP_LUT_G_ZVP_FL4_KO_BLOB_SIZE;
    ops.default_bundle_data = g_zvp_lut_default_bundle_blob;
    ops.default_bundle_len  = ZVP_LUT_G_ZVP_LUT_DEFAULT_BUNDLE_BLOB_SIZE;

    zvp_lut_ctx_t ctx;
    zvp_lut_init(&ctx, &ops);

    /* 1. 首次开机时默认选中关闭 LUT: 释放内置 18 款包至闪存，但延迟加载内核模块 */
    assert(zvp_lut_boot_activate(&ctx) == 0);
    assert(ctx.bundle_ready && ctx.bundle_from_embedded);
    assert(ctx.num_slots == 19u);
    assert(ctx.active_sel == 0u);
    assert(!ctx.kmod_loaded && g_mock_lut_kmod_calls == 0);
    assert(g_mock_lut_bundle_disk_len == ZVP_LUT_G_ZVP_LUT_DEFAULT_BUNDLE_BLOB_SIZE);

    /* 2. 损坏闪存包首字节 CRC 测试时应回退使用内嵌默认包 */
    g_mock_lut_bundle_disk[100] ^= 0x5Au;
    assert(zvp_lut_load_bundle_and_cfg(&ctx) == 0);
    assert(ctx.bundle_from_embedded);
    assert(ctx.num_slots == 19u);

    /* 3. 选中第 10 款 ClassicNeg: 触发按需加载 .ko + 上载 18 槽 + APPLY + 预览刷新 + 写 zvc.cfg */
    assert(zvp_lut_select_slot(&ctx, 10u, true, true) == 0);
    assert(ctx.kmod_loaded && ctx.catalog_uploaded);
    assert(g_mock_lut_kmod_calls == 1);
    assert(g_mock_lut_loaded_slots == 18u);
    assert(g_mock_lut_last_apply_sel == 10u);
    assert(g_mock_lut_refresh_calls == 1);
    assert(g_mock_lut_cfg_disk_len == sizeof(zvp_lut_cfg_disk_t));
    zvp_lut_adjust_t unpacked = zvp_lut_unpack_adjust(g_mock_lut_last_apply_bits);
    assert(unpacked.strength10 == ctx.adjust[10].strength10);

    /* 4. 模拟掉电重启 (重置软件状态，但保留内核已载状态与闪存 zvc.cfg/bundle) */
    zvp_lut_init(&ctx, &ops);
    assert(zvp_lut_boot_activate(&ctx) == 0);
    assert(!ctx.bundle_from_embedded);
    assert(ctx.active_sel == 10u);
    assert(ctx.kmod_loaded && ctx.catalog_uploaded);
    /* MATCH 命中时不应重复上传 18 条 slot */
    assert(g_mock_lut_last_apply_sel == 10u);

    /* 5. 安全关闭与卸载复位应写 sel=0 并触发刷新，且将 active_sel=0 落盘使重启后不恢复旧滤镜 */
    assert(zvp_lut_shutdown_reset(&ctx) == 0);
    assert(ctx.active_sel == 0u);
    assert(g_mock_lut_last_apply_sel == 0u);
    zvp_lut_init(&ctx, &ops);
    assert(zvp_lut_boot_activate(&ctx) == 0);
    assert(ctx.active_sel == 0u);
    /* 验证即使 kmod 尚未加载，安全复位同样覆盖旧配置 active_sel=0 */
    assert(zvp_lut_select_slot(&ctx, 10u, true, true) == 0);
    ctx.kmod_loaded = false;
    assert(zvp_lut_shutdown_reset(&ctx) == 0);
    zvp_lut_init(&ctx, &ops);
    assert(zvp_lut_boot_activate(&ctx) == 0);
    assert(ctx.active_sel == 0u);

    /* 6. 进程白名单过滤断言（防止误注入 sh/busybox 等非 im.elf 进程） */
    assert(zvp_is_target_im_exe_path("/usr/bin/im.elf"));
    assert(zvp_is_target_im_exe_path("im.elf"));
    assert(!zvp_is_target_im_exe_path("/bin/sh"));
    assert(!zvp_is_target_im_exe_path("/bin/busybox"));
    assert(!zvp_is_target_im_exe_path("/usr/bin/bootin.elf"));
    assert(!zvp_is_target_im_exe_path("/usr/bin/im.elf.bak"));
    assert(!zvp_is_target_im_exe_path(""));
    assert(!zvp_is_target_im_exe_path(NULL));

    /* 6. 滚到即预览延迟落盘 (persist_cfg=false -> cfg_dirty=true -> flush) */
    g_mock_lut_cfg_disk_len = 0u;
    assert(zvp_lut_select_slot(&ctx, 4u, false, true) == 0);
    assert(ctx.cfg_dirty);
    assert(g_mock_lut_cfg_disk_len == 0u);
    assert(zvp_lut_flush_pending_cfg(&ctx) == 0);
    assert(!ctx.cfg_dirty);
    assert(g_mock_lut_cfg_disk_len == sizeof(zvp_lut_cfg_disk_t));

    /* 7. 三级独立微调: 改7项参数/锐度(-5再减回原生)/取消回滚/保存持久化 */
    assert(!zvp_lut_slot_is_modified(&ctx, 4u));
    assert(zvp_lut_begin_fine_tune(&ctx, 4u) == 0);
    assert(ctx.fine_editing && ctx.fine_sel == 4u);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_CONTRAST, 2) == 0);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_STRENGTH, -3) == 0);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_FADE, 4) == 0);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_SHARPNESS, 1) == 0);
    assert(ctx.sharpness[4] == 1);
    assert(zvp_lut_slot_is_modified(&ctx, 4u));
    /* -5 往下再按1次应回到原生 */
    ctx.sharpness[4] = ZVP_LUT_SHARPNESS_MIN;
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_SHARPNESS, -1) == 0);
    assert(ctx.sharpness[4] == ZVP_LUT_SHARPNESS_NATIVE);
    /* 取消回滚后应恢复未改状态 */
    assert(zvp_lut_cancel_fine_tune(&ctx) == 0);
    assert(!ctx.fine_editing);
    assert(!zvp_lut_slot_is_modified(&ctx, 4u));

    /* 再次进入并修改对比度 +3 & 阴影 -2 -> 保存并重启验证 */
    assert(zvp_lut_begin_fine_tune(&ctx, 4u) == 0);
    int8_t expect_c = (int8_t)(ctx.default_adjust[4].contrast + 3);
    int8_t expect_sh = (int8_t)(ctx.default_adjust[4].shadows - 2);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_CONTRAST, 3) == 0);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_SHADOWS, -2) == 0);
    assert(zvp_lut_adjust_param_step(&ctx, ZVP_LUT_PARAM_SHARPNESS, 2) == 0);
    assert(zvp_lut_commit_fine_tune(&ctx) == 0);
    assert(!ctx.fine_editing && !ctx.cfg_dirty);

    zvp_lut_init(&ctx, &ops);
    assert(zvp_lut_boot_activate(&ctx) == 0);
    assert(ctx.active_sel == 4u);
    assert(ctx.adjust[4].contrast == expect_c);
    assert(ctx.adjust[4].shadows == expect_sh);
    assert(ctx.sharpness[4] == 2);
    assert(zvp_lut_slot_is_modified(&ctx, 4u));

    /* 8. 阶段3: SD 卡 ZVLOOK01 扫盘 + HOT_BEGIN/SELECT/COMMIT 热导入 + 失败 HOT_ABORT 回滚 + 一键恢复内置 */
    g_mock_lut_sd_disk_len = build_test_sd_bundle(g_mock_lut_sd_disk);
    zvp_lut_sd_item_t sd_items[ZVP_LUT_SD_MAX_ITEMS];
    uint32_t sd_cnt = 0u;
    assert(zvp_lut_scan_sd_bundles(&ctx, sd_items, ZVP_LUT_SD_MAX_ITEMS, &sd_cnt) == 0);
    assert(sd_cnt == 2u);
    assert(!sd_items[0].is_embedded && sd_items[0].look_count == 3u);
    assert(sd_items[1].is_embedded && sd_items[1].look_count == 18u);

    /* 8a. HOT_COMMIT 模拟空间不足失败 -> 必须触发 HOT_ABORT 并保持原 18 款目录不变 */
    uint32_t old_digest = ctx.bundle_digest;
    g_mock_lut_force_hot_err = -28L;
    assert(zvp_lut_hot_import_item(&ctx, &sd_items[0]) == -28);
    assert(g_mock_lut_hot_aborts == 1 && g_mock_lut_hot_commits == 0);
    assert(ctx.bundle_digest == old_digest && ctx.num_slots == 19u && ctx.active_sel == 4u);

    /* 8b. 正常热导入 SD 卡 3 款自定义包 -> 原子热切并默认选中第 1 款 Kodak2383 */
    g_mock_lut_force_hot_err = 0L;
    assert(zvp_lut_hot_import_item(&ctx, &sd_items[0]) == 0);
    assert(g_mock_lut_hot_commits == 1);
    assert(!ctx.bundle_from_embedded && ctx.num_slots == 4u && ctx.active_sel == 1u);
    assert(strcmp(ctx.look_ids[1], "custom-2383") == 0);
    assert(strcmp(ctx.look_names[1], "Kodak2383") == 0);
    assert(g_mock_lut_last_apply_sel == 1u);
    assert(g_mock_lut_bundle_disk_len == g_mock_lut_sd_disk_len);

    /* 8c. 点“内置默认”一键热恢复 18 款，且原有第 4 槽(zv1m2-fl)历史微调依然从 zvc.cfg 找回或正常切换 */
    assert(zvp_lut_hot_import_item(&ctx, &sd_items[1]) == 0);
    assert(g_mock_lut_hot_commits == 2);
    assert(ctx.bundle_from_embedded && ctx.num_slots == 19u && ctx.active_sel == 1u);
}

#include "zvp_menu_touch.h"

static void test_menu_touch_logic(void)
{
    /* 1. Fn 快捷菜单 2x6 网格命中与退出区校验 */
    assert(zvp_menu_touch_fn_tile_index(320, 120) == -1); /* 上方退出区域 */
    assert(zvp_menu_touch_fn_tile_index(0, 260) == 0);
    assert(zvp_menu_touch_fn_tile_index(109, 369) == 0);
    assert(zvp_menu_touch_fn_tile_index(110, 300) == 1);
    assert(zvp_menu_touch_fn_tile_index(530, 300) == 5);
    assert(zvp_menu_touch_fn_tile_index(639, 479) == 11);
    assert(zvp_menu_touch_fn_tile_index(250, 370) == 8);

    /* 2. 主菜单左右滑翻页 vs 点击容差判定 */
    assert(zvp_menu_touch_classify_main_swipe(-60, 10, true) == ZVP_MTF_GESTURE_SWIPE_LEFT);
    assert(zvp_menu_touch_classify_main_swipe(55, -12, true) == ZVP_MTF_GESTURE_SWIPE_RIGHT);
    assert(zvp_menu_touch_classify_main_swipe(-47, 0, true) == ZVP_MTF_GESTURE_TAP);
    assert(zvp_menu_touch_classify_main_swipe(-60, 35, true) == ZVP_MTF_GESTURE_TAP);
    assert(zvp_menu_touch_classify_main_swipe(-80, 0, false) == ZVP_MTF_GESTURE_TAP);

    /* 3. 二级弹窗上下滑滚动 vs 点击判定 */
    assert(zvp_menu_touch_classify_popup_swipe(5, -40, true) == ZVP_MTF_GESTURE_SWIPE_UP);
    assert(zvp_menu_touch_classify_popup_swipe(10, 42, true) == ZVP_MTF_GESTURE_SWIPE_DOWN);
    assert(zvp_menu_touch_classify_popup_swipe(20, 20, true) == ZVP_MTF_GESTURE_TAP);

    /* 4. Fn 12 格环形最短路径方向与步数 */
    int dir = -1;
    assert(zvp_menu_touch_fn_ring_steps(0u, 2u, &dir) == 2 && dir == 1);
    assert(zvp_menu_touch_fn_ring_steps(0u, 11u, &dir) == 1 && dir == 0);
    assert(zvp_menu_touch_fn_ring_steps(10u, 1u, &dir) == 3 && dir == 1);
    assert(zvp_menu_touch_fn_ring_steps(1u, 10u, &dir) == 3 && dir == 0);

    /* 5. 开关策略状态 */
    assert(zvp_menu_touch_is_enabled());
    zvp_menu_touch_set_enabled(false);
    assert(!zvp_menu_touch_is_enabled());
    zvp_menu_touch_set_enabled(true);
    assert(zvp_menu_touch_is_enabled());
}

#include "zvp_color_cal.h"

static uint8_t g_mock_vph_ram[0x1000];
static uint8_t g_mock_cal_slot0_lut[ZVP_CAL_LUT_BUF_BYTES];
static uint8_t g_mock_cal_slot0_mtxa[ZVP_CAL_MTXA_BYTES];
static uint8_t g_mock_cal_dirty_flag = 0u;
static uint32_t g_mock_cal_dirty_slot = 99u;
static uint8_t g_mock_cal_cfg_file[64];
static size_t  g_mock_cal_cfg_len = 0u;
static bool    g_mock_cal_cfg_exists = false;

static int mock_cal_phys_read(uint32_t pa, void *out_buf, size_t len)
{
    (void)g_mock_vph_ram;
    if (pa >= ZVP_CAL_VPH_PA_SLOT0_LUT &&
        pa + len <= ZVP_CAL_VPH_PA_SLOT0_LUT + sizeof(g_mock_cal_slot0_lut)) {
        memcpy(out_buf, g_mock_cal_slot0_lut + (pa - ZVP_CAL_VPH_PA_SLOT0_LUT), len);
        return 0;
    }
    if (pa >= ZVP_CAL_VPH_PA_SLOT0_MTXA &&
        pa + len <= ZVP_CAL_VPH_PA_SLOT0_MTXA + sizeof(g_mock_cal_slot0_mtxa)) {
        memcpy(out_buf, g_mock_cal_slot0_mtxa + (pa - ZVP_CAL_VPH_PA_SLOT0_MTXA), len);
        return 0;
    }
    if (pa == ZVP_CAL_VPH_PA_DIRTY_FLAG && len == 1u) {
        *(uint8_t *)out_buf = g_mock_cal_dirty_flag;
        return 0;
    }
    if (pa == ZVP_CAL_VPH_PA_DIRTY_SLOT && len == 4u) {
        *(uint32_t *)out_buf = g_mock_cal_dirty_slot;
        return 0;
    }
    return -1;
}

static int mock_cal_phys_write(uint32_t pa, const void *buf, size_t len)
{
    if (pa >= ZVP_CAL_VPH_PA_SLOT0_LUT &&
        pa + len <= ZVP_CAL_VPH_PA_SLOT0_LUT + sizeof(g_mock_cal_slot0_lut)) {
        memcpy(g_mock_cal_slot0_lut + (pa - ZVP_CAL_VPH_PA_SLOT0_LUT), buf, len);
        return 0;
    }
    if (pa >= ZVP_CAL_VPH_PA_SLOT0_MTXA &&
        pa + len <= ZVP_CAL_VPH_PA_SLOT0_MTXA + sizeof(g_mock_cal_slot0_mtxa)) {
        memcpy(g_mock_cal_slot0_mtxa + (pa - ZVP_CAL_VPH_PA_SLOT0_MTXA), buf, len);
        return 0;
    }
    if (pa == ZVP_CAL_VPH_PA_DIRTY_FLAG && len == 1u) {
        g_mock_cal_dirty_flag = *(const uint8_t *)buf;
        return 0;
    }
    if (pa == ZVP_CAL_VPH_PA_DIRTY_SLOT && len == 4u) {
        g_mock_cal_dirty_slot = *(const uint32_t *)buf;
        return 0;
    }
    return -1;
}

static int mock_cal_read_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (!path || strcmp(path, ZVP_CAL_CFG_PATH) != 0 || !g_mock_cal_cfg_exists) {
        return -2;
    }
    if (g_mock_cal_cfg_len > cap) return -7;
    memcpy(buf, g_mock_cal_cfg_file, g_mock_cal_cfg_len);
    if (out_len) *out_len = g_mock_cal_cfg_len;
    return 0;
}

static int mock_cal_write_file(const char *dir, const char *path, const void *buf, size_t len)
{
    (void)dir;
    if (!path || strcmp(path, ZVP_CAL_CFG_PATH) != 0 || len > sizeof(g_mock_cal_cfg_file)) {
        return -5;
    }
    memcpy(g_mock_cal_cfg_file, buf, len);
    g_mock_cal_cfg_len = len;
    g_mock_cal_cfg_exists = true;
    return 0;
}

static int mock_cal_remove_file(const char *path)
{
    if (path && strcmp(path, ZVP_CAL_CFG_PATH) == 0) {
        g_mock_cal_cfg_exists = false;
        g_mock_cal_cfg_len = 0u;
        return 0;
    }
    return -2;
}

static void init_mock_factory_slot0(void)
{
    memset(g_mock_cal_slot0_lut, 0xAA, sizeof(g_mock_cal_slot0_lut));
    uint16_t id_mtx[9] = { 512, 0, 0, 0, 512, 0, 0, 0, 512 };
    memcpy(g_mock_cal_slot0_mtxa, id_mtx, sizeof(id_mtx));

    for (uint32_t ch = 0u; ch < 3u; ch++) {
        uint16_t pts[ZVP_CAL_LUT_POINTS];
        for (uint32_t i = 0u; i < ZVP_CAL_LUT_POINTS; i++) {
            pts[i] = (uint16_t)((i == ZVP_CAL_LUT_POINTS - 1u) ? 2047u : (i * 64u));
        }
        pts[1] = (uint16_t)(80u + ch * 10u);
        memcpy(g_mock_cal_slot0_lut + ch * ZVP_CAL_LUT_CH_STRIDE, pts, ZVP_CAL_LUT_CH_BYTES);
    }
    g_mock_cal_dirty_flag = 0u;
    g_mock_cal_dirty_slot = 99u;
}

static int16_t s11(uint16_t v)
{
    return (v & 0x400u) ? (int16_t)((int32_t)(v & 0x7ffu) - 2048) : (int16_t)(v & 0x7ffu);
}

static void test_color_cal_logic(void)
{
    init_mock_factory_slot0();
    g_mock_cal_cfg_exists = false;
    g_mock_cal_cfg_len = 0u;

    zvp_color_cal_platform_ops_t ops = {
        .phys_read         = mock_cal_phys_read,
        .phys_write        = mock_cal_phys_write,
        .read_file         = mock_cal_read_file,
        .atomic_write_file = mock_cal_write_file,
        .remove_file       = mock_cal_remove_file,
    };

    zvp_color_cal_ctx_t ctx;
    zvp_color_cal_init(&ctx, &ops);
    assert(zvp_color_cal_boot_activate(&ctx) == 0);
    assert(ctx.factory_captured && !ctx.enabled);

    zvp_cal_hw_payload_t pay;
    zvp_cal_build_payload(&ctx.factory_slot0, &ctx.params, &pay);
    assert(memcmp(&pay, &ctx.factory_slot0, sizeof(pay)) == 0);

    assert(zvp_color_cal_step_param(&ctx, ZVP_CAL_PARAM_TEMP, 10, 1000u) == 0);
    assert(ctx.enabled && !ctx.compare_original && ctx.preset_idx == ZVP_CAL_PRESET_CUSTOM);
    assert(g_mock_cal_dirty_flag == 1u && g_mock_cal_dirty_slot == 0u);
    assert(g_mock_cal_slot0_lut[0x44] == 0xAA && g_mock_cal_slot0_lut[0x96] == 0xAA);

    uint16_t cur_mtx[9];
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    assert(s11(cur_mtx[ZVP_CAL_HW_CH_R * 3u + ZVP_CAL_HW_CH_R]) > 512);
    assert(s11(cur_mtx[ZVP_CAL_HW_CH_B * 3u + ZVP_CAL_HW_CH_B]) < 512);

    assert(zvp_color_cal_step_param(&ctx, ZVP_CAL_PARAM_GAMMA, 10, 1100u) == 0);
    uint16_t ch0_pts[ZVP_CAL_LUT_POINTS];
    memcpy(ch0_pts, g_mock_cal_slot0_lut, sizeof(ch0_pts));
    assert(ch0_pts[16] > ctx.factory_slot0.lut_ch[0][16]);
    assert(ch0_pts[0] == 0u && ch0_pts[32] == 2047u);

    assert(zvp_color_cal_step_param(&ctx, ZVP_CAL_PARAM_SAT, -20, 1200u) == 0);
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    assert(s11(cur_mtx[ZVP_CAL_HW_CH_G * 3u + ZVP_CAL_HW_CH_R]) > 0);
    int32_t white_out_g = s11(cur_mtx[ZVP_CAL_HW_CH_R * 3u + ZVP_CAL_HW_CH_G]) +
                          s11(cur_mtx[ZVP_CAL_HW_CH_G * 3u + ZVP_CAL_HW_CH_G]) +
                          s11(cur_mtx[ZVP_CAL_HW_CH_B * 3u + ZVP_CAL_HW_CH_G]);
    assert(white_out_g >= 500 && white_out_g <= 525);

    assert(zvp_color_cal_toggle_compare(&ctx, 1300u) == 0);
    assert(ctx.compare_original);
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    assert(cur_mtx[0] == 512 && cur_mtx[4] == 512 && cur_mtx[8] == 512);
    assert(zvp_color_cal_toggle_compare(&ctx, 1400u) == 0);
    assert(!ctx.compare_original);
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    int32_t out_sum_r = s11(cur_mtx[ZVP_CAL_HW_CH_R * 3u + ZVP_CAL_HW_CH_R]) +
                        s11(cur_mtx[ZVP_CAL_HW_CH_G * 3u + ZVP_CAL_HW_CH_R]) +
                        s11(cur_mtx[ZVP_CAL_HW_CH_B * 3u + ZVP_CAL_HW_CH_R]);
    int32_t out_sum_b = s11(cur_mtx[ZVP_CAL_HW_CH_R * 3u + ZVP_CAL_HW_CH_B]) +
                        s11(cur_mtx[ZVP_CAL_HW_CH_G * 3u + ZVP_CAL_HW_CH_B]) +
                        s11(cur_mtx[ZVP_CAL_HW_CH_B * 3u + ZVP_CAL_HW_CH_B]);
    assert(out_sum_r > 512 && out_sum_b < 512);

    zvp_color_cal_tick(&ctx, 1400u + 700u);
    assert(!ctx.cfg_dirty && g_mock_cal_cfg_exists);

    init_mock_factory_slot0();
    zvp_color_cal_tick(&ctx, 3000u);
    assert(ctx.reapply_count == 1u);
    assert(g_mock_cal_dirty_flag == 1u);
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    assert(s11(cur_mtx[ZVP_CAL_HW_CH_G * 3u + ZVP_CAL_HW_CH_R]) > 0);

    init_mock_factory_slot0();
    zvp_color_cal_ctx_t ctx2;
    zvp_color_cal_init(&ctx2, &ops);
    assert(zvp_color_cal_boot_activate(&ctx2) == 0);
    assert(ctx2.enabled && ctx2.params.vals[ZVP_CAL_PARAM_TEMP] == 10);
    assert(ctx2.params.vals[ZVP_CAL_PARAM_GAMMA] == 10);
    assert(ctx2.params.vals[ZVP_CAL_PARAM_SAT] == -20);

    g_mock_cal_cfg_file[sizeof(zvp_cal_cfg_disk_t) - 1u] ^= 0xFFu;
    zvp_color_cal_ctx_t ctx_bad;
    zvp_color_cal_init(&ctx_bad, &ops);
    assert(zvp_color_cal_boot_activate(&ctx_bad) != 0);
    assert(!ctx_bad.enabled);

    assert(zvp_color_cal_shutdown_reset(&ctx2, true) == 0);
    assert(!g_mock_cal_cfg_exists && !ctx2.enabled);
    memcpy(cur_mtx, g_mock_cal_slot0_mtxa, sizeof(cur_mtx));
    assert(cur_mtx[0] == 512 && cur_mtx[4] == 512 && cur_mtx[8] == 512);
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
    test_aspect_ratio_layout_and_state();
    test_craw_logic();
    test_craw_drainer_and_audio_ring();
    test_lut_core_and_bundle();
    test_menu_touch_logic();
    test_color_cal_logic();
    printf("[通过] zvp 全部核心逻辑单元测试通过！\n");
    return 0;
}
