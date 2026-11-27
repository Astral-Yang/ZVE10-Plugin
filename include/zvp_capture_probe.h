#ifndef ZVP_CAPTURE_PROBE_H
#define ZVP_CAPTURE_PROBE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 在 UI 主线程周期调用：把无锁探针环内收集到的唯一调用签名转成模块+偏移格式写入日志 */
void zvp_capture_probe_drain_logs(void);

/* 由 OsalSndMsg 钩子转交非音频/非高频心跳消息供只读去重测绘 */
void zvp_capture_probe_on_osal(uintptr_t lr, const char *tag,
                               uint32_t msg_id, const uint8_t *data, uint32_t len);

/* 获取只读探针环形队列与无锁去重统计（用于验证队列健康状态） */
void zvp_capture_probe_get_stats(uint32_t *out_push, uint32_t *out_drained,
                                 uint32_t *out_drop_lock, uint32_t *out_drop_hash,
                                 uint32_t *out_drop_full, int *out_lock);

/* 在开机稳定后（如看门狗解除后）再打开底层 OSAL C 原语采集，避开开机初始化喷发 */
void zvp_capture_probe_arm_osal(void);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_CAPTURE_PROBE_H */
