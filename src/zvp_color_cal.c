#include "zvp_color_cal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(zvp_cal_params_t) == 8, "zvp_cal_params_t must be 8 bytes");
_Static_assert(sizeof(zvp_cal_cfg_disk_t) == 24, "zvp_cal_cfg_disk_t must be 24 bytes");
#endif

#define ZVP_CAL_CFG_SAVE_DEBOUNCE_MS 600u
#define ZVP_CAL_GUARD_POLL_MS        500u

static const zvp_cal_preset_info_t s_cal_presets[ZVP_CAL_PRESET_COUNT] = {
    { "自定义调节",     "自定义", { {   0,   0,   0,   0,   0,   0,   0,   0 } } },
    { "D65 标准中性",   "D65标准", { {   6,   2,   0,   2,   0,   0,   0,   0 } } },
    { "苹果 P3 倾向",   "苹果风",  { {  10,   3,   5,   4,   4,   0,   0,   0 } } },
    { "清晰冷白监看",   "冷白",    { {  -8,   0,   4,   6,   2,   0,   0,   0 } } },
    { "户外高反差",     "高对比",  { {   2,   0,   6,  12,   5,   0,   0,   0 } } },
    { "低饱和灰度向",   "低饱和",  { {   0,   0, -14,   2,   0,   0,   0,   0 } } },
};

uint32_t zvp_cal_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = ~0u;
    for (size_t i = 0u; i < len; i++) {
        c ^= p[i];
        for (uint32_t j = 0u; j < 8u; j++) {
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
        }
    }
    return ~c;
}

int8_t zvp_cal_clamp_param(int val)
{
    if (val < ZVP_CAL_PARAM_MIN) return (int8_t)ZVP_CAL_PARAM_MIN;
    if (val > ZVP_CAL_PARAM_MAX) return (int8_t)ZVP_CAL_PARAM_MAX;
    return (int8_t)val;
}

const zvp_cal_preset_info_t *zvp_cal_get_presets(size_t *out_count)
{
    if (out_count) {
        *out_count = ZVP_CAL_PRESET_COUNT;
    }
    return s_cal_presets;
}

const zvp_cal_preset_info_t *zvp_cal_get_preset(uint8_t idx)
{
    if (idx >= ZVP_CAL_PRESET_COUNT) {
        return &s_cal_presets[0];
    }
    return &s_cal_presets[idx];
}

void zvp_cal_params_zero(zvp_cal_params_t *out_p)
{
    if (out_p) {
        memset(out_p, 0, sizeof(*out_p));
    }
}

static void init_default_linear_payload(zvp_cal_hw_payload_t *out_p)
{
    memset(out_p, 0, sizeof(*out_p));
    for (uint32_t ch = 0u; ch < 3u; ch++) {
        out_p->mtx_a[ch * 3u + ch] = (uint16_t)ZVP_CAL_MTX_UNIT_Q9;
        for (uint32_t i = 0u; i < ZVP_CAL_LUT_POINTS; i++) {
            uint32_t v = (i == ZVP_CAL_LUT_POINTS - 1u) ? ZVP_CAL_LUT_MAX_VAL : (i * 64u);
            out_p->lut_ch[ch][i] = (uint16_t)v;
        }
    }
}

static int16_t sign_ext_11bit(uint16_t raw)
{
    uint16_t v = raw & 0x7FFu;
    if (v & 0x400u) {
        return (int16_t)((int32_t)v - 2048);
    }
    return (int16_t)v;
}

static double clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static int32_t cal_round_i32(double v)
{
    return (v >= 0.0) ? (int32_t)(v + 0.5) : (int32_t)(v - 0.5);
}

/* 零 libm 外部符号依赖的高精度 exp(x), 避免引入 pow@GLIBC_2.29 等高版本符号 */
static double cal_exp(double x)
{
    static const double k_ln2 = 0.69314718055994530942;
    if (x < -20.0) return 0.0;
    if (x > 20.0) x = 20.0;

    int32_t k = cal_round_i32(x / k_ln2);
    double r = x - (double)k * k_ln2;
    double term = 1.0;
    double sum = 1.0;
    for (int32_t n = 1; n <= 12; n++) {
        term *= r / (double)n;
        sum += term;
    }
    while (k > 0) {
        sum *= 2.0;
        k--;
    }
    while (k < 0) {
        sum *= 0.5;
        k++;
    }
    return sum;
}

static double cal_ln(double x)
{
    static const double k_ln2 = 0.69314718055994530942;
    if (x <= 0.0) return -20.0;
    int32_t e = 0;
    while (x >= 1.4142135623730951) {
        x *= 0.5;
        e++;
    }
    while (x < 0.7071067811865475) {
        x *= 2.0;
        e--;
    }
    double u = (x - 1.0) / (x + 1.0);
    double u2 = u * u;
    double p = u;
    double sum = 0.0;
    for (int32_t n = 1; n <= 15; n += 2) {
        sum += p / (double)n;
        p *= u2;
    }
    return 2.0 * sum + (double)e * k_ln2;
}

static double cal_pow(double base, double exp_v)
{
    if (base <= 0.0) return 0.0;
    if (base == 1.0 || exp_v == 0.0) return 1.0;
    return cal_exp(exp_v * cal_ln(base));
}

static uint16_t pack_11bit_signed(double val_f)
{
    int32_t q = cal_round_i32(val_f * (double)ZVP_CAL_MTX_UNIT_Q9);
    if (q < -1024) q = -1024;
    if (q > 1023)  q = 1023;
    return (uint16_t)((uint32_t)q & 0x7FFu);
}

/*
 * 结合出厂原厂基准 (Matrix A + 3x33 1D LUT) 叠加用户实时校色参数：
 * 1) 1D LUT 负责 Gamma 幂次 + 平滑 S 曲线对比度（保留出厂非线性基准）；
 * 2) 3x3 CCM (Matrix A) 负责 BT.601 饱和度矩阵与亮度归一化 RGB 增益/色温/色调。
 */
void zvp_cal_build_payload(const zvp_cal_hw_payload_t *factory_base,
                           const zvp_cal_params_t *params,
                           zvp_cal_hw_payload_t *out_payload)
{
    if (!out_payload) {
        return;
    }

    zvp_cal_hw_payload_t base;
    if (factory_base) {
        base = *factory_base;
    } else {
        init_default_linear_payload(&base);
    }

    zvp_cal_params_t p;
    if (params) {
        p = *params;
    } else {
        zvp_cal_params_zero(&p);
    }

    bool all_zero = true;
    for (uint32_t i = 0u; i < ZVP_CAL_PARAM_COUNT; i++) {
        if (p.vals[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        *out_payload = base;
        return;
    }

    /* --- 1. 构造 1D LUT 曲线映射 (Gamma + Contrast)，叠加以出厂原厂曲线为输入 --- */
    double gamma_val = (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_GAMMA]);
    double contrast_val = (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_CONTRAST]);
    double gamma_exp = cal_pow(2.0, -gamma_val * 0.015);
    double contrast_k = contrast_val * 0.035; /* 正值 S 曲线拉大反差，负值压低反差 */

    for (uint32_t hw_ch = 0u; hw_ch < 3u; hw_ch++) {
        for (uint32_t i = 0u; i < ZVP_CAL_LUT_POINTS; i++) {
            double x = (double)(base.lut_ch[hw_ch][i] & ZVP_CAL_LUT_MAX_VAL) / (double)ZVP_CAL_LUT_MAX_VAL;
            x = clamp01(x);

            /* 保持绝对黑位与最大白位连续的同时施加 Gamma */
            double y = cal_pow(x, gamma_exp);

            /* 围绕 0.5 中灰的连续平滑反差曲线：y + k * (y - 0.5) * 4 * y * (1 - y) */
            if (contrast_k >= 0.0) {
                double s_term = 4.0 * y * (1.0 - y) * (y - 0.5);
                y = y + contrast_k * s_term;
            } else {
                /* 负对比度平滑向 0.5 收敛 */
                double blend = -contrast_val / 45.0; /* 最大约 0.67 柔和收敛 */
                y = y * (1.0 - blend) + 0.5 * blend;
            }

            y = clamp01(y);
            int32_t q = cal_round_i32(y * (double)ZVP_CAL_LUT_MAX_VAL);
            if (q < 0) q = 0;
            if (q > (int32_t)ZVP_CAL_LUT_MAX_VAL) q = (int32_t)ZVP_CAL_LUT_MAX_VAL;
            out_payload->lut_ch[hw_ch][i] = (uint16_t)q;
        }
    }

    /* --- 2. 构造逻辑 RGB 线性校色矩阵：Gain(Temp/Tint/RGB) * Saturation * OrigMtxA --- */
    double temp = (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_TEMP]);
    double tint = (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_TINT]);
    double sat  = 1.0 + (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_SAT]) * 0.025;
    double gr   = 1.0 + (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_GAIN_R]) * 0.012;
    double gg   = 1.0 + (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_GAIN_G]) * 0.012;
    double gb   = 1.0 + (double)zvp_cal_clamp_param(p.vals[ZVP_CAL_PARAM_GAIN_B]) * 0.012;

    double w_r = gr * (1.0 + temp * 0.011 + tint * 0.004);
    double w_g = gg * (1.0 - tint * 0.010);
    double w_b = gb * (1.0 - temp * 0.011 + tint * 0.004);

    if (w_r < 0.25) w_r = 0.25;
    if (w_g < 0.25) w_g = 0.25;
    if (w_b < 0.25) w_b = 0.25;

    /* 当用户未独立大幅调高某色时，按人眼亮度权重保持色温/色调偏置后画面整体亮度恒定；
     * 若显式调整 R/G/B 增益，保留增益与色温叠加后的相对比值并防止 Q2.9 溢出 [0.2, 1.85] */
    double luma_norm = 0.299 * w_r + 0.587 * w_g + 0.114 * w_b;
    double avg_gain = 0.299 * gr + 0.587 * gg + 0.114 * gb;
    if (luma_norm > 0.01) {
        double corr = avg_gain / luma_norm;
        w_r *= corr;
        w_g *= corr;
        w_b *= corr;
    }

    static const double k_luma_w[3] = { 0.299, 0.587, 0.114 };
    const double gains[3] = { w_r, w_g, w_b };
    double user_rgb[3][3];
    for (uint32_t r = 0u; r < 3u; r++) {
        for (uint32_t c = 0u; c < 3u; c++) {
            double sat_term = (1.0 - sat) * k_luma_w[c] + ((r == c) ? sat : 0.0);
            user_rgb[r][c] = gains[r] * sat_term;
        }
    }

    /* 经真机实测确认：VPH Matrix A 硬件 3x3 系数采用转置布局（行代表输入通道 c、列代表输出通道 r，
     * 即 out[r] = sum_c mtx_a[c * 3 + r] * in[c]）。因此此处按 [k_hw_map[c] * 3 + k_hw_map[r]] 读写。 */
    static const uint32_t k_hw_map[3] = { ZVP_CAL_HW_CH_R, ZVP_CAL_HW_CH_G, ZVP_CAL_HW_CH_B };
    double orig_rgb[3][3];
    for (uint32_t r = 0u; r < 3u; r++) {
        for (uint32_t c = 0u; c < 3u; c++) {
            uint16_t raw = base.mtx_a[k_hw_map[c] * 3u + k_hw_map[r]];
            orig_rgb[r][c] = (double)sign_ext_11bit(raw) / (double)ZVP_CAL_MTX_UNIT_Q9;
        }
    }

    /* out_rgb = user_rgb * orig_rgb，按硬件转置布局写入 [R, G, B] */
    for (uint32_t r = 0u; r < 3u; r++) {
        for (uint32_t c = 0u; c < 3u; c++) {
            double acc = 0.0;
            for (uint32_t k = 0u; k < 3u; k++) {
                acc += user_rgb[r][k] * orig_rgb[k][c];
            }
            out_payload->mtx_a[k_hw_map[c] * 3u + k_hw_map[r]] = pack_11bit_signed(acc);
        }
    }
}

static int read_slot0_hw(const zvp_color_cal_platform_ops_t *ops, zvp_cal_hw_payload_t *out_p)
{
    if (!ops || !ops->phys_read || !out_p) {
        return -EINVAL;
    }
    int rc = ops->phys_read(ZVP_CAL_VPH_PA_SLOT0_MTXA, out_p->mtx_a, ZVP_CAL_MTXA_BYTES);
    if (rc != 0) {
        return rc;
    }
    for (uint32_t ch = 0u; ch < 3u; ch++) {
        rc = ops->phys_read(ZVP_CAL_VPH_PA_SLOT0_LUT + ch * ZVP_CAL_LUT_CH_STRIDE,
                            out_p->lut_ch[ch], ZVP_CAL_LUT_CH_BYTES);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int write_slot0_and_trigger(const zvp_color_cal_platform_ops_t *ops, const zvp_cal_hw_payload_t *p)
{
    if (!ops || !ops->phys_write || !p) {
        return -EINVAL;
    }
    int rc = ops->phys_write(ZVP_CAL_VPH_PA_SLOT0_MTXA, p->mtx_a, ZVP_CAL_MTXA_BYTES);
    if (rc != 0) {
        return rc;
    }
    for (uint32_t ch = 0u; ch < 3u; ch++) {
        rc = ops->phys_write(ZVP_CAL_VPH_PA_SLOT0_LUT + ch * ZVP_CAL_LUT_CH_STRIDE,
                             p->lut_ch[ch], ZVP_CAL_LUT_CH_BYTES);
        if (rc != 0) {
            return rc;
        }
    }

    /* 同步写 dirty slot=0 并置位 dirty flag=1，由 VPH 主循环 0x63a5e118 -> 0x63dcbecc 原生写入硬件 */
    uint32_t slot = 0u;
    uint8_t dirty = 1u;
    rc = ops->phys_write(ZVP_CAL_VPH_PA_DIRTY_SLOT, &slot, sizeof(slot));
    if (rc != 0) {
        return rc;
    }
    return ops->phys_write(ZVP_CAL_VPH_PA_DIRTY_FLAG, &dirty, sizeof(dirty));
}

void zvp_color_cal_init(zvp_color_cal_ctx_t *ctx, const zvp_color_cal_platform_ops_t *ops)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (ops) {
        ctx->ops = *ops;
    }
    init_default_linear_payload(&ctx->factory_slot0);
    ctx->preset_idx = ZVP_CAL_PRESET_CUSTOM;
    zvp_cal_params_zero(&ctx->params);
    ctx->initialized = true;
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "未开启 · 使用出厂面板校准");
}

int zvp_color_cal_capture_factory(zvp_color_cal_ctx_t *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }
    if (ctx->factory_captured) {
        return 0;
    }
    if (!ctx->ops.phys_read) {
        return -ENOSYS;
    }
    zvp_cal_hw_payload_t snap;
    int rc = read_slot0_hw(&ctx->ops, &snap);
    if (rc == 0) {
        /* 仅当读出数据为有效 11-bit 单调曲线且非全零时锁定为出厂基准 */
        if (snap.lut_ch[0][ZVP_CAL_LUT_POINTS - 1u] >= 1024u &&
            snap.lut_ch[1][ZVP_CAL_LUT_POINTS - 1u] >= 1024u &&
            snap.lut_ch[2][ZVP_CAL_LUT_POINTS - 1u] >= 1024u) {
            ctx->factory_slot0 = snap;
            ctx->factory_captured = true;
        } else {
            rc = -EILSEQ;
        }
    }
    return rc;
}

static int apply_internal(zvp_color_cal_ctx_t *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }
    if (!ctx->factory_captured) {
        int crc = zvp_color_cal_capture_factory(ctx);
        if (crc != 0) {
            ctx->last_apply_rc = crc;
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "读取原厂 Slot0 失败 (%d)", crc);
            return crc;
        }
    }

    zvp_cal_hw_payload_t target;
    if (ctx->enabled && !ctx->compare_original) {
        zvp_cal_build_payload(&ctx->factory_slot0, &ctx->params, &target);
    } else {
        target = ctx->factory_slot0;
    }

    int rc = write_slot0_and_trigger(&ctx->ops, &target);
    ctx->last_apply_rc = rc;
    if (rc == 0) {
        ctx->applied_slot0 = target;
        ctx->has_applied = true;
        if (!ctx->enabled) {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已关闭 · 还原出厂屏幕校色");
        } else if (ctx->compare_original) {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "对比原色中 · 临时显示出厂色彩");
        } else {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已生效 · %s",
                     zvp_cal_get_preset(ctx->preset_idx)->name);
        }
    } else {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "下发 VPH 槽位失败 (%d)", rc);
    }
    return rc;
}

int zvp_color_cal_boot_activate(zvp_color_cal_ctx_t *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }
    (void)zvp_color_cal_capture_factory(ctx);

    if (!ctx->ops.read_file) {
        return 0;
    }

    zvp_cal_cfg_disk_t cfg;
    size_t rlen = 0u;
    memset(&cfg, 0, sizeof(cfg));
    int rc = ctx->ops.read_file(ZVP_CAL_CFG_PATH, &cfg, sizeof(cfg), &rlen);
    if (rc != 0 || rlen == 0u) {
        /* 首次运行尚未落盘 cal.cfg 属正常态，保持默认未开启 */
        return 0;
    }
    if (rlen != sizeof(cfg) ||
        cfg.magic != ZVP_CAL_CFG_MAGIC || cfg.version != ZVP_CAL_CFG_VERSION) {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "配置格式异常，已忽略");
        return -EBADMSG;
    }

    uint32_t expect_crc = zvp_cal_crc32(&cfg, offsetof(zvp_cal_cfg_disk_t, crc32));
    if (cfg.crc32 != expect_crc) {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "配置 CRC 校验失败，已忽略");
        return -EBADMSG;
    }

    ctx->enabled = (cfg.enabled != 0u);
    ctx->compare_original = false;
    ctx->preset_idx = (cfg.preset_idx < ZVP_CAL_PRESET_COUNT) ? cfg.preset_idx : ZVP_CAL_PRESET_CUSTOM;
    for (uint32_t i = 0u; i < ZVP_CAL_PARAM_COUNT; i++) {
        ctx->params.vals[i] = zvp_cal_clamp_param(cfg.params.vals[i]);
    }
    ctx->cfg_dirty = false;

    if (ctx->enabled) {
        return apply_internal(ctx);
    }
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已读取配置 · 当前未开启");
    return 0;
}

int zvp_color_cal_apply(zvp_color_cal_ctx_t *ctx, bool persist_cfg, uint64_t now_ms)
{
    if (!ctx) {
        return -EINVAL;
    }
    int rc = apply_internal(ctx);
    if (persist_cfg) {
        ctx->cfg_dirty = true;
        ctx->cfg_dirty_ms = now_ms;
    }
    return rc;
}

int zvp_color_cal_set_enabled(zvp_color_cal_ctx_t *ctx, bool enable, bool persist_cfg, uint64_t now_ms)
{
    if (!ctx) {
        return -EINVAL;
    }
    ctx->enabled = enable;
    if (!enable) {
        ctx->compare_original = false;
    }
    int rc = apply_internal(ctx);
    if (persist_cfg) {
        ctx->cfg_dirty = true;
        ctx->cfg_dirty_ms = now_ms;
        (void)zvp_color_cal_flush_cfg(ctx);
    }
    return rc;
}

int zvp_color_cal_toggle_compare(zvp_color_cal_ctx_t *ctx, uint64_t now_ms)
{
    (void)now_ms;
    if (!ctx) {
        return -EINVAL;
    }
    if (!ctx->enabled) {
        /* 关闭状态下按“对比原色”直接开启校色方便即时对比 */
        ctx->enabled = true;
        ctx->compare_original = false;
        ctx->cfg_dirty = true;
        ctx->cfg_dirty_ms = now_ms;
    } else {
        ctx->compare_original = !ctx->compare_original;
    }
    return apply_internal(ctx);
}

int zvp_color_cal_select_preset(zvp_color_cal_ctx_t *ctx, uint8_t preset_idx, bool persist_cfg, uint64_t now_ms)
{
    if (!ctx || preset_idx >= ZVP_CAL_PRESET_COUNT) {
        return -EINVAL;
    }
    ctx->preset_idx = preset_idx;
    if (preset_idx != ZVP_CAL_PRESET_CUSTOM) {
        ctx->params = s_cal_presets[preset_idx].params;
    }
    ctx->enabled = true;
    ctx->compare_original = false;
    int rc = apply_internal(ctx);
    if (persist_cfg) {
        ctx->cfg_dirty = true;
        ctx->cfg_dirty_ms = now_ms;
    }
    return rc;
}

int zvp_color_cal_step_param(zvp_color_cal_ctx_t *ctx, zvp_cal_param_t param, int delta, uint64_t now_ms)
{
    if (!ctx || (uint32_t)param >= ZVP_CAL_PARAM_COUNT) {
        return -EINVAL;
    }
    ctx->params.vals[param] = zvp_cal_clamp_param((int)ctx->params.vals[param] + delta);
    ctx->preset_idx = ZVP_CAL_PRESET_CUSTOM;
    ctx->enabled = true;
    ctx->compare_original = false;
    ctx->cfg_dirty = true;
    ctx->cfg_dirty_ms = now_ms;
    return apply_internal(ctx);
}

int zvp_color_cal_reset_params(zvp_color_cal_ctx_t *ctx, bool persist_cfg, uint64_t now_ms)
{
    if (!ctx) {
        return -EINVAL;
    }
    zvp_cal_params_zero(&ctx->params);
    ctx->preset_idx = ZVP_CAL_PRESET_CUSTOM;
    ctx->compare_original = false;
    int rc = apply_internal(ctx);
    if (persist_cfg) {
        ctx->cfg_dirty = true;
        ctx->cfg_dirty_ms = now_ms;
        (void)zvp_color_cal_flush_cfg(ctx);
    }
    return rc;
}

void zvp_color_cal_tick(zvp_color_cal_ctx_t *ctx, uint64_t now_ms)
{
    if (!ctx || !ctx->initialized) {
        return;
    }

    if (ctx->cfg_dirty && now_ms >= ctx->cfg_dirty_ms &&
        (now_ms - ctx->cfg_dirty_ms) >= ZVP_CAL_CFG_SAVE_DEBOUNCE_MS) {
        (void)zvp_color_cal_flush_cfg(ctx);
    }

    /* 周期防覆盖巡检与延迟就绪重试 */
    if (ctx->enabled && !ctx->compare_original &&
        (ctx->last_guard_poll_ms == 0u ||
         (now_ms >= ctx->last_guard_poll_ms &&
          (now_ms - ctx->last_guard_poll_ms) >= ZVP_CAL_GUARD_POLL_MS))) {
        ctx->last_guard_poll_ms = now_ms;

        if (!ctx->factory_captured) {
            if (apply_internal(ctx) == 0) {
                ctx->reapply_count++;
            }
            return;
        }

        if (ctx->has_applied && ctx->ops.phys_read) {
            uint16_t cur_mtx[ZVP_CAL_MTX_ELEMS];
            uint16_t cur_ch0_pt1 = 0u;
            if (ctx->ops.phys_read(ZVP_CAL_VPH_PA_SLOT0_MTXA, cur_mtx, sizeof(cur_mtx)) == 0 &&
                ctx->ops.phys_read(ZVP_CAL_VPH_PA_SLOT0_LUT + 2u, &cur_ch0_pt1, sizeof(cur_ch0_pt1)) == 0) {
                if (memcmp(cur_mtx, ctx->applied_slot0.mtx_a, sizeof(cur_mtx)) != 0 ||
                    cur_ch0_pt1 != ctx->applied_slot0.lut_ch[0][1]) {
                    if (write_slot0_and_trigger(&ctx->ops, &ctx->applied_slot0) == 0) {
                        ctx->reapply_count++;
                    }
                }
            }
        }
    }
}

int zvp_color_cal_flush_cfg(zvp_color_cal_ctx_t *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }
    ctx->cfg_dirty = false;
    if (!ctx->ops.atomic_write_file) {
        return -ENOSYS;
    }

    zvp_cal_cfg_disk_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = ZVP_CAL_CFG_MAGIC;
    cfg.version = ZVP_CAL_CFG_VERSION;
    cfg.enabled = ctx->enabled ? 1u : 0u;
    cfg.preset_idx = (ctx->preset_idx < ZVP_CAL_PRESET_COUNT) ? ctx->preset_idx : ZVP_CAL_PRESET_CUSTOM;
    cfg.params = ctx->params;
    cfg.crc32 = zvp_cal_crc32(&cfg, offsetof(zvp_cal_cfg_disk_t, crc32));
    return ctx->ops.atomic_write_file(ZVP_CAL_CFG_DIR, ZVP_CAL_CFG_PATH, &cfg, sizeof(cfg));
}

int zvp_color_cal_shutdown_reset(zvp_color_cal_ctx_t *ctx, bool delete_cfg)
{
    if (!ctx) {
        return -EINVAL;
    }
    ctx->enabled = false;
    ctx->compare_original = false;
    ctx->cfg_dirty = false;
    zvp_cal_params_zero(&ctx->params);
    ctx->preset_idx = ZVP_CAL_PRESET_CUSTOM;

    int rc = 0;
    if (ctx->factory_captured) {
        rc = write_slot0_and_trigger(&ctx->ops, &ctx->factory_slot0);
        ctx->applied_slot0 = ctx->factory_slot0;
        ctx->has_applied = (rc == 0);
    }
    if (delete_cfg && ctx->ops.remove_file) {
        (void)ctx->ops.remove_file(ZVP_CAL_CFG_PATH);
    }
    return rc;
}
