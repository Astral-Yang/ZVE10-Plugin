#include "zvp_core.h"
#include "zvp_abi.h"

#include <stdio.h>

static bool check_flag_file_on_drive(const zvp_gate_ops_t *ops, char drv, const char *name)
{
    char path[48];
    snprintf(path, sizeof(path), "%c:/%s", drv, name);
    if (ops->file_exists(path)) {
        return true;
    }
    /* 兼容 Windows 新建空文件误保留 .txt 后缀的场景 */
    snprintf(path, sizeof(path), "%c:/%s.TXT", drv, name);
    if (ops->file_exists(path)) {
        return true;
    }
    return false;
}

static bool has_flag_file_anywhere(const zvp_gate_ops_t *ops, uint32_t drives, const char *name)
{
    for (uint32_t d = 0u; d < 26u; d++) {
        if ((drives & (1u << d)) != 0u) {
            if (check_flag_file_on_drive(ops, (char)('A' + d), name)) {
                return true;
            }
        }
    }
    char p[48];
    snprintf(p, sizeof(p), "B:/%s", name);
    if (ops->file_exists(p)) return true;
    snprintf(p, sizeof(p), "/mnt/sd/%s", name);
    if (ops->file_exists(p)) return true;
    snprintf(p, sizeof(p), "/tmp/%s", name);
    if (ops->file_exists(p)) return true;
    return false;
}

zvp_gate_status_t zvp_gate_evaluate_boot(const zvp_gate_ops_t *ops,
                                         uint32_t timeout_ms,
                                         uint32_t poll_interval_ms)
{
    if (!ops || !ops->get_mount_drives || !ops->file_exists) {
        return ZVP_GATE_DISABLED_NO_CARD;
    }

    if (poll_interval_ms == 0) {
        poll_interval_ms = 100u;
    }

    uint32_t elapsed = 0;
    uint32_t last_drives = 0;
    bool sd_mounted = false;

    while (1) {
        last_drives = ops->get_mount_drives();
        /* 跳过机内虚拟盘 A: (bit 0)，检测真实 SD 卡槽 B: (bit 1) */
        if (last_drives & ZVP_FSYS_DRIVE_SD_MASK) {
            sd_mounted = true;
            break;
        }
        if (elapsed >= timeout_ms) {
            break;
        }
        uint32_t step = timeout_ms - elapsed;
        if (step > poll_interval_ms) {
            step = poll_interval_ms;
        }
        if (ops->sleep_ms) {
            ops->sleep_ms(step);
        }
        elapsed += step;
    }

    /* 未插卡时保持静默禁用（纯原厂行为） */
    if (!sd_mounted) {
        return ZVP_GATE_DISABLED_NO_CARD;
    }

    /* 1. 优先检查彻底卸载标志文件 ZVP_UNINSTALL（清理配置并解除 preload） */
    if (has_flag_file_anywhere(ops, last_drives, ZVP_GATE_UNINSTALL_NAME)) {
        return ZVP_GATE_DISABLED_UNINSTALL;
    }

    /* 2. 检测到 SD 卡根目录禁用标志文件 ZVP_OFF 时保持静默禁用并复位 Backup */
    if (has_flag_file_anywhere(ops, last_drives, ZVP_GATE_DISABLE_FILE_NAME)) {
        return ZVP_GATE_DISABLED_KILL_FILE;
    }

    return ZVP_GATE_ALLOW;
}

const char *zvp_gate_status_str(zvp_gate_status_t st)
{
    switch (st) {
        case ZVP_GATE_ALLOW:
            return "ALLOW";
        case ZVP_GATE_DISABLED_NO_CARD:
            return "DISABLED_NO_CARD";
        case ZVP_GATE_DISABLED_KILL_FILE:
            return "DISABLED_KILL_FILE";
        case ZVP_GATE_DISABLED_UNINSTALL:
            return "DISABLED_UNINSTALL";
        default:
            return "UNKNOWN";
    }
}
