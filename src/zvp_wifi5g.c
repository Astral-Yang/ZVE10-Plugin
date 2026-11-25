#include "zvp_core.h"
#include "zvp_abi.h"
#include <string.h>
#include <stdio.h>

static const uint8_t s_channels[ZVP_WIFI5G_CHANNEL_COUNT] = {
    ZVP_WIFI_CH_AUTO,
    36u, 40u, 44u, 48u,
    149u, 153u, 157u, 161u, 165u
};

static void set_msg(zvp_wifi5g_ctx_t *ctx, const char *msg)
{
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "%s", msg ? msg : "");
}

void zvp_wifi5g_init(zvp_wifi5g_ctx_t *ctx, const zvp_wifi5g_platform_ops_t *ops)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = ZVP_PATCH_IDLE;
    ctx->enabled = false;
    ctx->selected_channel = ZVP_WIFI_CH_AUTO;
    memcpy(ctx->orig_netsvc, ZVP_ORIG_ISVALID_5GHZ, sizeof(ctx->orig_netsvc));
    ctx->orig_caution = ZVP_ORIG_CAUTION_BYTE;
    ctx->orig_enable_bk = 0;
    ctx->orig_nvram_go = 0x09u;
    ctx->orig_nvram_sta = 0x09u;
    ctx->orig_band_menu = 0x00u;
    ctx->orig_ap_ch_band[0] = ZVP_WIFI_BAND_24G;
    ctx->orig_ap_ch_band[1] = ZVP_WIFI_CH_AUTO;
    if (ops) {
        ctx->ops = *ops;
    }
    set_msg(ctx, "待机 (原厂 2.4GHz)");
}

const uint8_t *zvp_wifi5g_get_channels(size_t *out_count)
{
    if (out_count) {
        *out_count = ZVP_WIFI5G_CHANNEL_COUNT;
    }
    return s_channels;
}

bool zvp_wifi5g_is_valid_channel(uint8_t ch)
{
    for (size_t i = 0; i < ZVP_WIFI5G_CHANNEL_COUNT; ++i) {
        if (s_channels[i] == ch) {
            return true;
        }
    }
    return false;
}

static bool is_valid_24g_ap_ch_band(const uint8_t pair[2])
{
    if (pair[0] == ZVP_WIFI_BAND_24G) {
        return true;
    }
    return (pair[0] == 0x00u && pair[1] == 0x00u);
}

static void capture_initial_backup_if_needed(zvp_wifi5g_ctx_t *ctx)
{
    if (ctx->backup_initialized || !ctx->ops.backup_read) {
        return;
    }
    uint8_t pair[2] = { ZVP_WIFI_BAND_24G, ZVP_WIFI_CH_AUTO };
    if (ctx->ops.backup_read(ZVP_BKID_WIFI_AP_CH_BAND, pair) >= 0) {
        if (is_valid_24g_ap_ch_band(pair)) {
            ctx->orig_ap_ch_band[0] = pair[0];
            ctx->orig_ap_ch_band[1] = pair[1];
        } else {
            /* 自愈：清理历史版本反序写入或残留 5G 频段导致未激活状态错位的值 */
            ctx->orig_ap_ch_band[0] = ZVP_WIFI_BAND_24G;
            ctx->orig_ap_ch_band[1] = ZVP_WIFI_CH_AUTO;
            if (!ctx->enabled && ctx->ops.backup_write) {
                uint8_t clean[2] = { ZVP_WIFI_BAND_24G, ZVP_WIFI_CH_AUTO };
                (void)ctx->ops.backup_write(ZVP_BKID_SUBSYS_NET, ZVP_BKID_WIFI_AP_CH_BAND, clean);
            }
        }
    }
    uint8_t nv_go = 0;
    if (ctx->ops.backup_read(ZVP_BKID_WIFI_NVRAM_GO, &nv_go) >= 0 &&
        nv_go >= 1u && nv_go <= 26u) {
        ctx->orig_nvram_go = nv_go;
    }
    uint8_t nv_sta = 0;
    if (ctx->ops.backup_read(ZVP_BKID_WIFI_NVRAM_STA, &nv_sta) >= 0 &&
        nv_sta >= 1u && nv_sta <= 26u) {
        ctx->orig_nvram_sta = nv_sta;
    }
    uint8_t band_menu = 0;
    if (ctx->ops.backup_read(ZVP_BKID_WIFI_BAND_MENU, &band_menu) >= 0) {
        ctx->orig_band_menu = band_menu ? 1u : 0u;
    }
    ctx->backup_initialized = true;
}

static int apply_caution_patch(zvp_wifi5g_ctx_t *ctx)
{
    if (ctx->caution_patched) {
        return 0;
    }
    if (!ctx->ops.find_module_base || !ctx->ops.read_mem || !ctx->ops.write_mem) {
        return -1;
    }
    uintptr_t base = ctx->ops.find_module_base("CautionConfig.so");
    if (!base) {
        /* 某些配置下 CautionConfig.so 可能按需加载；非致命，保留后续重试 */
        return 1;
    }
    uintptr_t target = base + ZVP_OFF_CAUTION_WIFI_CASE;
    uint8_t cur = 0;
    if (ctx->ops.read_mem(target, &cur, 1) != 0) {
        set_msg(ctx, "读取 CautionConfig 失败");
        return -1;
    }
    if (cur == ZVP_PATCH_CAUTION_BYTE) {
        ctx->orig_caution = ZVP_ORIG_CAUTION_BYTE;
        ctx->caution_patched = true;
        return 0;
    }
    if (cur != ZVP_ORIG_CAUTION_BYTE) {
        set_msg(ctx, "CautionConfig 字节校验不匹配");
        return -1;
    }
    ctx->orig_caution = cur;
    uint8_t patched = ZVP_PATCH_CAUTION_BYTE;
    if (ctx->ops.write_mem(target, &patched, 1) != 0) {
        set_msg(ctx, "写入 CautionConfig 失败");
        return -1;
    }
    ctx->caution_patched = true;
    return 0;
}

static int apply_netsvc_patch(zvp_wifi5g_ctx_t *ctx)
{
    if (ctx->net_patched) {
        return 0;
    }
    if (!ctx->ops.find_module_base || !ctx->ops.read_mem || !ctx->ops.write_mem) {
        return -1;
    }
    uintptr_t base = ctx->ops.find_module_base("libNetService.so");
    if (!base) {
        return 1; /* 尚未加载，等待懒加载 */
    }
    uintptr_t target = base + ZVP_OFF_NETSVC_ISVALID_5GHZ;
    uint8_t cur[4] = { 0 };
    if (ctx->ops.read_mem(target, cur, sizeof(cur)) != 0) {
        set_msg(ctx, "读取 libNetService 失败");
        return -1;
    }
    if (memcmp(cur, ZVP_PATCH_ISVALID_5GHZ, 4) == 0) {
        memcpy(ctx->orig_netsvc, ZVP_ORIG_ISVALID_5GHZ, 4);
        ctx->net_patched = true;
        return 0;
    }
    if (memcmp(cur, ZVP_ORIG_ISVALID_5GHZ, 4) != 0) {
        set_msg(ctx, "isValidWifi5GHz 原机器码不匹配");
        return -1;
    }
    memcpy(ctx->orig_netsvc, cur, 4);
    if (ctx->ops.write_mem(target, ZVP_PATCH_ISVALID_5GHZ, 4) != 0) {
        set_msg(ctx, "写入 isValidWifi5GHz 补丁失败");
        return -1;
    }
    ctx->net_patched = true;
    return 0;
}

static int write_wifi_backup_pair(zvp_wifi5g_ctx_t *ctx, uint8_t band, uint8_t ch)
{
    if (!ctx->ops.backup_write) {
        return -1;
    }
    /* 0x013E003A 为 Ch36 区域允许位，保持出厂表不动；0x013E0061 格式为 [band, channel] */
    uint8_t pair[2];
    pair[0] = band;
    pair[1] = ch;
    int rc = ctx->ops.backup_write(ZVP_BKID_SUBSYS_NET, ZVP_BKID_WIFI_AP_CH_BAND, pair);
    return (rc >= 0) ? 0 : -1;
}

static int write_backup_byte(zvp_wifi5g_ctx_t *ctx, uint32_t subsys, uint32_t bkid, uint8_t val)
{
    if (!ctx->ops.backup_write) {
        return -1;
    }
    return (ctx->ops.backup_write(subsys, bkid, &val) >= 0) ? 0 : -1;
}

static int write_nvram_region_and_menu(zvp_wifi5g_ctx_t *ctx, uint8_t nv_go, uint8_t nv_sta, uint8_t band_menu)
{
    if (write_backup_byte(ctx, ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_GO, nv_go) != 0) {
        return -1;
    }
    if (write_backup_byte(ctx, ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_STA, nv_sta) != 0) {
        return -1;
    }
    if (write_backup_byte(ctx, ZVP_BKID_SUBSYS_SYS, ZVP_BKID_WIFI_BAND_MENU, band_menu) != 0) {
        return -1;
    }
    return 0;
}

static void refresh_active_state(zvp_wifi5g_ctx_t *ctx)
{
    int rc_net = apply_netsvc_patch(ctx);
    if (rc_net < 0) {
        ctx->state = ZVP_PATCH_ERROR;
        return;
    }
    int rc_caut = apply_caution_patch(ctx);
    if (rc_caut < 0) {
        ctx->state = ZVP_PATCH_ERROR;
        return;
    }

    if (write_nvram_region_and_menu(ctx,
                                    ZVP_WIFI_NVRAM_CN_GO,
                                    ZVP_WIFI_NVRAM_CN_STA,
                                    0x01u) != 0) {
        ctx->state = ZVP_PATCH_ERROR;
        set_msg(ctx, "写入 NVRAM 地区 Backup 失败");
        return;
    }

    if (write_wifi_backup_pair(ctx, ZVP_WIFI_BAND_5G, ctx->selected_channel) != 0) {
        ctx->state = ZVP_PATCH_ERROR;
        set_msg(ctx, "写入 Wi-Fi 频段 Backup 失败");
        return;
    }

    if (rc_net == 1) {
        ctx->state = ZVP_PATCH_WAIT_NETSVC;
        set_msg(ctx, "已开启 · 等待网络服务加载");
    } else {
        ctx->state = ZVP_PATCH_ACTIVE;
        if (ctx->selected_channel == ZVP_WIFI_CH_AUTO) {
            set_msg(ctx, "5GHz 已开启 · XR76/143 · Auto");
        } else {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg),
                     "5GHz 已开启 · XR76/143 · %u", (unsigned)ctx->selected_channel);
        }
    }
}

int zvp_wifi5g_set_enabled(zvp_wifi5g_ctx_t *ctx, bool enable)
{
    if (!ctx) {
        return -1;
    }
    capture_initial_backup_if_needed(ctx);

    if (!enable) {
        ctx->enabled = false;
        uint8_t restore_ch = (ctx->backup_initialized &&
                              ctx->orig_ap_ch_band[0] == ZVP_WIFI_BAND_24G &&
                              ctx->orig_ap_ch_band[1] != 0x00u)
                                 ? ctx->orig_ap_ch_band[1]
                                 : ZVP_WIFI_CH_AUTO;
        if (write_wifi_backup_pair(ctx, ZVP_WIFI_BAND_24G, restore_ch) != 0 ||
            write_nvram_region_and_menu(ctx,
                                        ctx->orig_nvram_go,
                                        ctx->orig_nvram_sta,
                                        ctx->orig_band_menu) != 0) {
            ctx->state = ZVP_PATCH_ERROR;
            set_msg(ctx, "切换回 2.4GHz 失败");
            return -1;
        }
        ctx->state = ZVP_PATCH_IDLE;
        set_msg(ctx, "已切换回 2.4GHz 默认频段");
        return 0;
    }

    ctx->enabled = true;
    refresh_active_state(ctx);
    return (ctx->state == ZVP_PATCH_ERROR) ? -1 : 0;
}

int zvp_wifi5g_select_channel(zvp_wifi5g_ctx_t *ctx, uint8_t ch)
{
    if (!ctx || !zvp_wifi5g_is_valid_channel(ch)) {
        return -1;
    }
    ctx->selected_channel = ch;
    if (!ctx->enabled) {
        return 0;
    }
    refresh_active_state(ctx);
    return (ctx->state == ZVP_PATCH_ERROR) ? -1 : 0;
}

void zvp_wifi5g_poll(zvp_wifi5g_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    capture_initial_backup_if_needed(ctx);
    if (!ctx->enabled) {
        return;
    }
    if (ctx->state == ZVP_PATCH_WAIT_NETSVC || !ctx->net_patched || !ctx->caution_patched) {
        refresh_active_state(ctx);
    }
}
