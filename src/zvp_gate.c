#include "zvp_core.h"
#include "zvp_abi.h"

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
    bool sd_mounted = false;

    while (1) {
        uint32_t drives = ops->get_mount_drives();
        if (drives & ZVP_FSYS_DRIVE_A_BIT) {
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

    /* 检测到 SD 卡根目录禁用标志文件 ZVP_OFF 时保持静默禁用 */
    if (ops->file_exists(ZVP_GATE_DISABLE_PATH_FSYS) ||
        ops->file_exists(ZVP_GATE_DISABLE_PATH_POSIX1)) {
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
        default:
            return "UNKNOWN";
    }
}
