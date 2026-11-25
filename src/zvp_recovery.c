#include "zvp_core.h"
#include "zvp_abi.h"
#include <string.h>
#include <stdio.h>

void zvp_recovery_init(zvp_recovery_ctx_t *rec)
{
    if (!rec) {
        return;
    }
    memset(rec, 0, sizeof(*rec));
    rec->phase = ZVP_REC_IDLE;
    snprintf(rec->status_text, sizeof(rec->status_text), "就绪 (双击全量恢复默认)");
}

void zvp_recovery_set_full_reset_cb(zvp_recovery_ctx_t *rec,
                                    zvp_recovery_extra_fn cb,
                                    void *user_data)
{
    if (!rec) {
        return;
    }
    rec->full_reset_cb = cb;
    rec->full_reset_user = user_data;
}

void zvp_recovery_tick(zvp_recovery_ctx_t *rec, uint64_t now_ms)
{
    if (!rec) {
        return;
    }
    if (rec->phase == ZVP_REC_ARMED) {
        if (now_ms >= rec->armed_at_ms &&
            (now_ms - rec->armed_at_ms) >= ZVP_RECOVERY_ARM_TIMEOUT_MS) {
            rec->phase = ZVP_REC_IDLE;
            snprintf(rec->status_text, sizeof(rec->status_text), "已取消确认，再次点击开始复位");
        }
    }
}

static int restore_bytes(zvp_wifi5g_ctx_t *wifi)
{
    int err = 0;

    if (wifi->ops.find_module_base && wifi->ops.read_mem && wifi->ops.write_mem) {
        uintptr_t base = wifi->ops.find_module_base("libNetService.so");
        if (base) {
            uintptr_t target = base + ZVP_OFF_NETSVC_ISVALID_5GHZ;
            uint8_t cur[4] = { 0 };
            if (wifi->ops.read_mem(target, cur, 4) == 0) {
                if (memcmp(cur, ZVP_PATCH_ISVALID_5GHZ, 4) == 0) {
                    if (wifi->ops.write_mem(target, wifi->orig_netsvc, 4) != 0) {
                        err = -1;
                    } else {
                        wifi->net_patched = false;
                    }
                } else if (memcmp(cur, wifi->orig_netsvc, 4) == 0) {
                    wifi->net_patched = false;
                } else if (wifi->net_patched) {
                    err = -1;
                }
            } else if (wifi->net_patched) {
                err = -1;
            }
        } else {
            wifi->net_patched = false;
        }
    }

    if (wifi->ops.find_module_base && wifi->ops.read_mem && wifi->ops.write_mem) {
        uintptr_t base = wifi->ops.find_module_base("CautionConfig.so");
        if (base) {
            uintptr_t target = base + ZVP_OFF_CAUTION_WIFI_CASE;
            uint8_t cur = 0;
            if (wifi->ops.read_mem(target, &cur, 1) == 0) {
                if (cur == ZVP_PATCH_CAUTION_BYTE) {
                    if (wifi->ops.write_mem(target, &wifi->orig_caution, 1) != 0) {
                        err = -1;
                    } else {
                        wifi->caution_patched = false;
                    }
                } else if (cur == wifi->orig_caution) {
                    wifi->caution_patched = false;
                } else if (wifi->caution_patched) {
                    err = -1;
                }
            } else if (wifi->caution_patched) {
                err = -1;
            }
        } else {
            wifi->caution_patched = false;
        }
    }

    return err;
}

zvp_recovery_phase_t zvp_recovery_trigger(zvp_recovery_ctx_t *rec,
                                          zvp_wifi5g_ctx_t *wifi,
                                          uint64_t now_ms)
{
    if (!rec || !wifi) {
        return ZVP_REC_FAILED;
    }

    zvp_recovery_tick(rec, now_ms);

    if (rec->phase != ZVP_REC_ARMED) {
        rec->phase = ZVP_REC_ARMED;
        rec->armed_at_ms = now_ms;
        snprintf(rec->status_text, sizeof(rec->status_text), "再次点击确认全量复位 (3秒内)");
        return rec->phase;
    }

    /* 二次确认窗口内，执行内存补丁、Backup 与全量功能状态原子回滚 */
    int rc_mem = restore_bytes(wifi);
    int rc_bk = 0;
    int rc_extra = 0;

    if (wifi->ops.backup_write) {
        uint8_t orig_pair[2];
        orig_pair[0] = ZVP_WIFI_BAND_24G;
        orig_pair[1] = (wifi->backup_initialized &&
                        wifi->orig_ap_ch_band[0] == ZVP_WIFI_BAND_24G &&
                        wifi->orig_ap_ch_band[1] != 0x00u)
                           ? wifi->orig_ap_ch_band[1]
                           : ZVP_WIFI_CH_AUTO;
        if (wifi->ops.backup_write(ZVP_BKID_SUBSYS_NET, ZVP_BKID_WIFI_AP_CH_BAND, orig_pair) < 0) {
            rc_bk = -1;
        }
        if (wifi->ops.backup_write(ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_GO, &wifi->orig_nvram_go) < 0) {
            rc_bk = -1;
        }
        if (wifi->ops.backup_write(ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_STA, &wifi->orig_nvram_sta) < 0) {
            rc_bk = -1;
        }
        if (wifi->ops.backup_write(ZVP_BKID_SUBSYS_SYS, ZVP_BKID_WIFI_BAND_MENU, &wifi->orig_band_menu) < 0) {
            rc_bk = -1;
        }
    }

    wifi->enabled = false;
    wifi->selected_channel = ZVP_WIFI_CH_AUTO;

    if (rec->full_reset_cb) {
        if (rec->full_reset_cb(rec->full_reset_user) != 0) {
            rc_extra = -1;
        }
    }

    if (rc_mem == 0 && rc_bk == 0 && rc_extra == 0) {
        wifi->state = ZVP_PATCH_IDLE;
        snprintf(wifi->last_msg, sizeof(wifi->last_msg), "待机 (已恢复原厂 2.4GHz)");
        rec->phase = ZVP_REC_DONE;
        snprintf(rec->status_text, sizeof(rec->status_text), "已全量回退所有修改与配置");
    } else {
        wifi->state = ZVP_PATCH_ERROR;
        rec->phase = ZVP_REC_FAILED;
        snprintf(rec->status_text, sizeof(rec->status_text), "复位部分失败，建议重启相机");
    }

    return rec->phase;
}
