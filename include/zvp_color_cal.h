#ifndef ZVP_COLOR_CAL_H
#define ZVP_COLOR_CAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sony ZV-E10 VPH 面板硬件校色引擎协议定义 (基于真机 DRAM 与 av-cam.bin 实证)
 * - 纯 RAM 活跃工作槽位注入，绝不写入 Backup Flash
 * - 通过 0xf3445300 (3x3 11-bit CCM) + 0xf3445400 (3x33 11-bit RGB 1D LUT)
 *   仅作用于机背 LCD (PanelOut)，不影响拍照 JPG/RAW、录像文件与 HDMI 输出
 * ========================================================================== */

#define ZVP_CAL_VPH_PA_ACTIVE_SLOT   0x0441fd54u /* uint32_t: 当前激活槽位 (0..4) */
#define ZVP_CAL_VPH_PA_CALIB_EN      0x04522f80u /* uint8_t: 校准总开关 */
#define ZVP_CAL_VPH_PA_SUB_EN        0x0452287cu /* uint8_t: LUT/Matrix 子开关 */
#define ZVP_CAL_VPH_PA_MTXA_OVR      0x04522bc2u /* uint8_t: MatrixA 覆盖开关 */
#define ZVP_CAL_VPH_PA_DIRTY_FLAG    0x0441bb68u /* uint8_t: [0x6441af40+0xc28] 置1触发下发 */
#define ZVP_CAL_VPH_PA_DIRTY_SLOT    0x0441bb6cu /* uint32_t: [0x6441af40+0xc2c] 下发槽位 */

#define ZVP_CAL_VPH_PA_SLOT0_LUT     0x044c0eecu /* 256B LUT 槽位 (Ch0=+0x00, Ch1=+0x50, Ch2=+0xa0) */
#define ZVP_CAL_VPH_PA_SLOT0_MTX     0x044be732u /* 112B Matrix 槽位 (MatrixA=+0x2a..+0x3a) */
#define ZVP_CAL_VPH_PA_SLOT0_MTXA    (ZVP_CAL_VPH_PA_SLOT0_MTX + 0x2au) /* 0x044be75c: 9*uint16_t */

#define ZVP_CAL_LUT_POINTS           33u
#define ZVP_CAL_LUT_CH_STRIDE        0x50u       /* 80 字节 (实际前 66 字节为 33 x uint16_t) */
#define ZVP_CAL_LUT_CH_BYTES         (ZVP_CAL_LUT_POINTS * 2u) /* 66 字节 */
#define ZVP_CAL_LUT_BUF_BYTES        256u
#define ZVP_CAL_MTX_ELEMS            9u
#define ZVP_CAL_MTXA_BYTES           18u
#define ZVP_CAL_MTX_UNIT_Q9          512         /* 0x200 = 1.0 (Q2.9) */
#define ZVP_CAL_LUT_MAX_VAL          2047u       /* 11-bit 上限 0x7ff */

/* 由真机实测验证：Slot0 MatrixA 与 1D LUT 硬件通道序为标准 [R, G, B]
 * （旧版曾误按 MatrixB 的 Y/Cb/Cr 列序设为 [G, B, R]，实测确认 R/G/B 错位一位） */
#define ZVP_CAL_HW_CH_R              0u
#define ZVP_CAL_HW_CH_G              1u
#define ZVP_CAL_HW_CH_B              2u

#define ZVP_CAL_PARAM_MIN            (-30)
#define ZVP_CAL_PARAM_MAX            30

#define ZVP_CAL_CFG_DIR              "/setting/zvc"
#define ZVP_CAL_CFG_PATH             "/setting/zvc/cal.cfg"
#define ZVP_CAL_CFG_MAGIC            0x4C41435Au /* "ZCAL" */
#define ZVP_CAL_CFG_VERSION          1u

typedef enum {
    ZVP_CAL_PARAM_TEMP = 0,     /* 色温: 负冷蓝 / 正暖黄 */
    ZVP_CAL_PARAM_TINT = 1,     /* 色调: 负偏绿 / 正品红 */
    ZVP_CAL_PARAM_SAT = 2,      /* 饱和度 */
    ZVP_CAL_PARAM_CONTRAST = 3, /* 对比度 */
    ZVP_CAL_PARAM_GAMMA = 4,    /* 伽马 (中间调明暗) */
    ZVP_CAL_PARAM_GAIN_R = 5,   /* R 增益 */
    ZVP_CAL_PARAM_GAIN_G = 6,   /* G 增益 */
    ZVP_CAL_PARAM_GAIN_B = 7,   /* B 增益 */
    ZVP_CAL_PARAM_COUNT = 8
} zvp_cal_param_t;

typedef enum {
    ZVP_CAL_PRESET_CUSTOM = 0,
    ZVP_CAL_PRESET_D65_STD = 1,
    ZVP_CAL_PRESET_APPLE_P3 = 2,
    ZVP_CAL_PRESET_COOL_CLEAR = 3,
    ZVP_CAL_PRESET_HIGH_CONTRAST = 4,
    ZVP_CAL_PRESET_LOW_SAT = 5,
    ZVP_CAL_PRESET_COUNT = 6
} zvp_cal_preset_t;

typedef struct zvp_cal_params {
    int8_t vals[ZVP_CAL_PARAM_COUNT];
} zvp_cal_params_t;

typedef struct zvp_cal_preset_info {
    const char      *name;
    const char      *short_name;
    zvp_cal_params_t params;
} zvp_cal_preset_info_t;

typedef struct zvp_cal_hw_payload {
    uint16_t mtx_a[ZVP_CAL_MTX_ELEMS];      /* 硬件通道序 3x3 11-bit 有符号 Q2.9 (0x7ff mask) */
    uint16_t lut_ch[3][ZVP_CAL_LUT_POINTS]; /* 硬件通道序 3x33 点 11-bit (0..2047) */
} zvp_cal_hw_payload_t;

typedef struct zvp_cal_cfg_disk {
    uint32_t         magic;
    uint32_t         version;
    uint8_t          enabled;
    uint8_t          preset_idx;
    uint8_t          reserved[2];
    zvp_cal_params_t params;
    uint32_t         crc32;
} zvp_cal_cfg_disk_t;

typedef struct zvp_color_cal_platform_ops {
    int (*phys_read)(uint32_t pa, void *out_buf, size_t len);
    int (*phys_write)(uint32_t pa, const void *buf, size_t len);
    int (*read_file)(const char *path, void *buf, size_t cap, size_t *out_len);
    int (*atomic_write_file)(const char *dir, const char *path, const void *buf, size_t len);
    int (*remove_file)(const char *path);
} zvp_color_cal_platform_ops_t;

typedef struct zvp_color_cal_ctx {
    bool     initialized;
    bool     factory_captured;
    bool     enabled;
    bool     compare_original; /* 仅内存态“一键对比原色”开关，不覆盖已调参数 */
    bool     cfg_dirty;
    uint64_t cfg_dirty_ms;
    uint64_t last_guard_poll_ms;

    uint8_t          preset_idx;
    zvp_cal_params_t params;

    /* 启动后首次只读采集到的真机出厂 Slot 0 基准 */
    zvp_cal_hw_payload_t factory_slot0;
    /* 最近一次成功下发到 Slot 0 的目标包，用于防覆盖指纹比对 */
    zvp_cal_hw_payload_t applied_slot0;
    bool                 has_applied;

    uint32_t reapply_count;
    int      last_apply_rc;
    char     last_msg[96];
    zvp_color_cal_platform_ops_t ops;
} zvp_color_cal_ctx_t;

/* 纯数学与参数工具接口 */
uint32_t zvp_cal_crc32(const void *data, size_t len);
int8_t   zvp_cal_clamp_param(int val);
const zvp_cal_preset_info_t *zvp_cal_get_presets(size_t *out_count);
const zvp_cal_preset_info_t *zvp_cal_get_preset(uint8_t idx);
void     zvp_cal_params_zero(zvp_cal_params_t *out_p);
void     zvp_cal_build_payload(const zvp_cal_hw_payload_t *factory_base,
                               const zvp_cal_params_t *params,
                               zvp_cal_hw_payload_t *out_payload);

/* 生命周期与真机交互控制接口 */
void zvp_color_cal_init(zvp_color_cal_ctx_t *ctx, const zvp_color_cal_platform_ops_t *ops);
int  zvp_color_cal_capture_factory(zvp_color_cal_ctx_t *ctx);
int  zvp_color_cal_boot_activate(zvp_color_cal_ctx_t *ctx);
int  zvp_color_cal_apply(zvp_color_cal_ctx_t *ctx, bool persist_cfg, uint64_t now_ms);
int  zvp_color_cal_set_enabled(zvp_color_cal_ctx_t *ctx, bool enable, bool persist_cfg, uint64_t now_ms);
int  zvp_color_cal_toggle_compare(zvp_color_cal_ctx_t *ctx, uint64_t now_ms);
int  zvp_color_cal_select_preset(zvp_color_cal_ctx_t *ctx, uint8_t preset_idx, bool persist_cfg, uint64_t now_ms);
int  zvp_color_cal_step_param(zvp_color_cal_ctx_t *ctx, zvp_cal_param_t param, int delta, uint64_t now_ms);
int  zvp_color_cal_reset_params(zvp_color_cal_ctx_t *ctx, bool persist_cfg, uint64_t now_ms);
void zvp_color_cal_tick(zvp_color_cal_ctx_t *ctx, uint64_t now_ms);
int  zvp_color_cal_flush_cfg(zvp_color_cal_ctx_t *ctx);
int  zvp_color_cal_shutdown_reset(zvp_color_cal_ctx_t *ctx, bool delete_cfg);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_COLOR_CAL_H */
