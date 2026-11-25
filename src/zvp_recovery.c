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
    int rc_bk = zvp_recovery_restore_backup_vals(
        &wifi->ops, wifi->backup_initialized,
        wifi->orig_nvram_go, wifi->orig_nvram_sta,
        wifi->orig_band_menu, wifi->orig_ap_ch_band);
    int rc_extra = 0;

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

int zvp_recovery_restore_backup_vals(const zvp_wifi5g_platform_ops_t *ops,
                                     bool backup_initialized,
                                     uint8_t orig_nvram_go,
                                     uint8_t orig_nvram_sta,
                                     uint8_t orig_band_menu,
                                     const uint8_t orig_ap_ch_band[2])
{
    if (!ops || !ops->backup_write) {
        return 0;
    }
    int rc_bk = 0;
    uint8_t orig_pair[2];
    orig_pair[0] = ZVP_WIFI_BAND_24G;
    orig_pair[1] = (backup_initialized && orig_ap_ch_band &&
                    orig_ap_ch_band[0] == ZVP_WIFI_BAND_24G &&
                    orig_ap_ch_band[1] != 0x00u)
                       ? orig_ap_ch_band[1]
                       : ZVP_WIFI_CH_AUTO;
    uint8_t nv_go = (orig_nvram_go >= 1u && orig_nvram_go <= 26u) ? orig_nvram_go : 0x09u;
    uint8_t nv_sta = (orig_nvram_sta >= 1u && orig_nvram_sta <= 26u) ? orig_nvram_sta : 0x09u;
    uint8_t bmenu = orig_band_menu ? 1u : 0u;

    if (ops->backup_write(ZVP_BKID_SUBSYS_NET, ZVP_BKID_WIFI_AP_CH_BAND, orig_pair) < 0) {
        rc_bk = -1;
    }
    if (ops->backup_write(ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_GO, &nv_go) < 0) {
        rc_bk = -1;
    }
    if (ops->backup_write(ZVP_BKID_SUBSYS_NVRAM, ZVP_BKID_WIFI_NVRAM_STA, &nv_sta) < 0) {
        rc_bk = -1;
    }
    if (ops->backup_write(ZVP_BKID_SUBSYS_SYS, ZVP_BKID_WIFI_BAND_MENU, &bmenu) < 0) {
        rc_bk = -1;
    }
    return rc_bk;
}

#define ZVP_BOOT_GUARD_MAGIC   0x5A564247u
#define ZVP_BOOT_GUARD_VERSION 1u

static void load_boot_guard_state(const zvp_boot_guard_ops_t *ops,
                                  const char *guard_path,
                                  zvp_boot_guard_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->magic = ZVP_BOOT_GUARD_MAGIC;
    st->version = ZVP_BOOT_GUARD_VERSION;
    if (!ops || !ops->read_file || !guard_path) {
        return;
    }
    zvp_boot_guard_state_t disk;
    size_t got = 0u;
    if (ops->read_file(guard_path, &disk, sizeof(disk), &got) == 0 &&
        got == sizeof(disk) &&
        disk.magic == ZVP_BOOT_GUARD_MAGIC &&
        disk.version == ZVP_BOOT_GUARD_VERSION) {
        *st = disk;
    }
}

bool zvp_boot_guard_disable_preload(const zvp_boot_guard_ops_t *ops,
                                    const char *dmode_path,
                                    const char *preload_path)
{
    if (!ops || !ops->write_file) {
        return false;
    }
    bool ok = true;
    if (dmode_path && dmode_path[0]) {
        if (ops->write_file(dmode_path, "3\n", 2u) != 0) {
            ok = false;
        }
    }
    if (preload_path && preload_path[0]) {
        if (ops->write_file(preload_path, "\n", 1u) != 0) {
            ok = false;
        }
    }
    return ok;
}

bool zvp_boot_guard_enter(const zvp_boot_guard_ops_t *ops,
                          const char *guard_path,
                          const char *dmode_path,
                          const char *preload_path,
                          uint32_t max_fails,
                          zvp_boot_guard_state_t *out_state)
{
    zvp_boot_guard_state_t st;
    load_boot_guard_state(ops, guard_path, &st);
    if (max_fails == 0u) {
        max_fails = 2u;
    }

    bool tripped = false;
    if (st.fail_count >= max_fails) {
        tripped = true;
        (void)zvp_boot_guard_disable_preload(ops, dmode_path, preload_path);
        st.fail_count = 0u;
        st.trip_count++;
    } else {
        st.fail_count++;
    }

    if (ops && ops->write_file && guard_path) {
        (void)ops->write_file(guard_path, &st, sizeof(st));
    }
    if (out_state) {
        *out_state = st;
    }
    return tripped;
}

void zvp_boot_guard_mark_healthy(const zvp_boot_guard_ops_t *ops,
                                 const char *guard_path)
{
    if (!ops || !ops->write_file || !guard_path) {
        return;
    }
    zvp_boot_guard_state_t st;
    load_boot_guard_state(ops, guard_path, &st);
    if (st.fail_count != 0u) {
        st.fail_count = 0u;
        (void)ops->write_file(guard_path, &st, sizeof(st));
    }
}
