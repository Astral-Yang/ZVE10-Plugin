#ifndef ZVP_LUT_H
#define ZVP_LUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sony ZV-E10 Custom LUT (zvc_fl4.ko + ZVLOOK01) 协议与数据结构定义
 * 与 dino2048/ZV-E10-Custom-LUT ABI 保持字节级一致
 * ========================================================================== */

#define ZVP_LUT_MAX_DYN_LOOKS     24u
#define ZVP_LUT_VISIBLE_LOOKS     18u
#define ZVP_LUT_NUM_SLOTS         (ZVP_LUT_VISIBLE_LOOKS + 1u) /* 0=关闭, 1..18=LUT */
#define ZVP_LUT_ID_BYTES          32u
#define ZVP_LUT_NAME_BYTES        64u

#define ZVP_LUT_FD_SEL            0x7E57u
#define ZVP_LUT_FD_MASK           0x7E58u
#define ZVP_LUT_FD_STAT           0x7E59u
#define ZVP_LUT_FD_ADJ            0x7E5Au
#define ZVP_LUT_FD_TONE           0x7E5Bu
#define ZVP_LUT_FD_LOAD           0x7E5Cu
#define ZVP_LUT_FD_APPLY          0x7E5Du
#define ZVP_LUT_FD_CATALOG        0x7E5Eu

#define ZVP_LUT_CAT_COMMIT        0x0100u
#define ZVP_LUT_CAT_ABORT         0x0101u
#define ZVP_LUT_CAT_MATCH         0x0102u
#define ZVP_LUT_CAT_HOT_BEGIN     0x0200u
#define ZVP_LUT_CAT_HOT_SELECT    0x0240u
#define ZVP_LUT_CAT_HOT_COMMIT    0x0260u
#define ZVP_LUT_CAT_HOT_ABORT     0x0261u

#define ZVP_LUT_BUNDLE_MAGIC      "ZVLOOK01"
#define ZVP_LUT_BUNDLE_HEADER     20u
#define ZVP_LUT_BUNDLE_ENTRY      2156u
#define ZVP_LUT_BUNDLE_MAX_BYTES  (ZVP_LUT_BUNDLE_HEADER + ZVP_LUT_VISIBLE_LOOKS * ZVP_LUT_BUNDLE_ENTRY + 4u)

#define ZVP_LUT_CFG_MAGIC         0x4343565Au /* "ZVCC" */
#define ZVP_LUT_CFG_VERSION       5u
#define ZVP_LUT_CFG_V4_CRC_OFF    724u
#define ZVP_LUT_SHARPNESS_NATIVE  127
#define ZVP_LUT_SHARPNESS_MIN     (-5)
#define ZVP_LUT_SHARPNESS_MAX     5

#define ZVP_LUT_DIR_PATH          "/setting/zvc"
#define ZVP_LUT_BUNDLE_PATH       "/setting/zvc/zve10_looks.bin"
#define ZVP_LUT_CFG_PATH          "/setting/zvc/zvc.cfg"

#define ZVP_LUT_SD_MAX_ITEMS      16u
#define ZVP_LUT_SD_NAME_LEN       64u
#define ZVP_LUT_SD_PATH_LEN       128u

typedef struct zvp_lut_sd_item {
    char     name[ZVP_LUT_SD_NAME_LEN];
    char     path[ZVP_LUT_SD_PATH_LEN];
    uint32_t size;
    uint8_t  look_count;
    bool     is_embedded;
} zvp_lut_sd_item_t;

typedef struct zvp_lut_adjust {
    int8_t  contrast;
    int8_t  saturation;
    int8_t  highlights;
    int8_t  shadows;
    uint8_t fade;
    uint8_t strength10;
} zvp_lut_adjust_t;

typedef struct zvp_lut_slot_packet {
    uint32_t slot_idx;
    uint32_t grid_enabled;
    int16_t  matrix[9];
    int16_t  coef[8];
    uint16_t pad0;
    uint32_t gamma96[96];
    uint32_t grid1620[405];
} zvp_lut_slot_packet_t;

typedef struct zvp_lut_cfg_look {
    char             id[ZVP_LUT_ID_BYTES];
    zvp_lut_adjust_t adjust;
} zvp_lut_cfg_look_t;

typedef struct zvp_lut_cfg_disk {
    uint32_t           magic;
    uint32_t           version;
    char               selected_id[ZVP_LUT_ID_BYTES];
    zvp_lut_cfg_look_t looks[ZVP_LUT_VISIBLE_LOOKS];
    int8_t             sharpness[ZVP_LUT_VISIBLE_LOOKS];
    uint8_t            reserved[2];
    uint32_t           crc32;
} zvp_lut_cfg_disk_t;

typedef struct zvp_lut_preview_info {
    int32_t rc;
    int32_t style;
    int32_t box;
    int32_t contrast;
    int32_t saturation;
    int32_t stock_sharpness;
    int32_t applied_sharpness;
} zvp_lut_preview_info_t;

typedef enum {
    ZVP_LUT_PARAM_STRENGTH   = 0,
    ZVP_LUT_PARAM_CONTRAST   = 1,
    ZVP_LUT_PARAM_HIGHLIGHTS = 2,
    ZVP_LUT_PARAM_SHADOWS    = 3,
    ZVP_LUT_PARAM_FADE       = 4,
    ZVP_LUT_PARAM_SATURATION = 5,
    ZVP_LUT_PARAM_SHARPNESS  = 6,
    ZVP_LUT_PARAM_COUNT      = 7
} zvp_lut_param_t;

typedef struct zvp_lut_platform_ops {
    int  (*init_module)(const void *ko_data, size_t ko_len, const char *opts);
    long (*sys_write_fd)(uint32_t fd, uintptr_t arg1, uintptr_t arg2);
    int  (*read_file)(const char *path, void *buf, size_t cap, size_t *out_len);
    int  (*atomic_write_file)(const char *dir, const char *path, const void *buf, size_t len);
    int  (*refresh_preview)(int8_t req_sharpness, int8_t *io_native_sharpness, zvp_lut_preview_info_t *out_info);
    int  (*scan_sd_bundles)(zvp_lut_sd_item_t *out_items, uint32_t max_items, uint32_t *out_count);
    int  (*read_sd_file)(const char *path, void *buf, size_t cap, size_t *out_len);
    const void *default_ko_data;
    size_t      default_ko_len;
    const void *default_bundle_data;
    size_t      default_bundle_len;
} zvp_lut_platform_ops_t;

typedef struct zvp_lut_ctx {
    bool     initialized;
    bool     bundle_ready;
    bool     bundle_from_embedded;
    bool     kmod_loaded;
    bool     catalog_uploaded;
    bool     cfg_dirty;

    /* 三级独立微调草稿会话与取消回滚快照 */
    bool             fine_editing;
    uint32_t         fine_sel;
    zvp_lut_adjust_t fine_saved_adjust;
    int8_t           fine_saved_sharpness;

    uint32_t bundle_digest;
    uint32_t bundle_size;
    uint32_t num_slots; /* 1..19 (0=关闭 LUT, 1..count=自定义滤镜) */
    uint32_t active_sel;
    int8_t   native_sharpness;

    char             look_ids[ZVP_LUT_NUM_SLOTS][ZVP_LUT_ID_BYTES];
    char             look_names[ZVP_LUT_NUM_SLOTS][ZVP_LUT_NAME_BYTES];
    zvp_lut_adjust_t default_adjust[ZVP_LUT_NUM_SLOTS];
    zvp_lut_adjust_t adjust[ZVP_LUT_NUM_SLOTS];
    int8_t           sharpness[ZVP_LUT_NUM_SLOTS];

    int      last_kmod_rc;
    int      last_catalog_rc;
    int      last_apply_rc;
    int      last_cfg_rc;
    zvp_lut_preview_info_t last_preview;
    char     last_msg[128];

    uint8_t  bundle_bytes[ZVP_LUT_BUNDLE_MAX_BYTES];
    zvp_lut_platform_ops_t ops;
} zvp_lut_ctx_t;

/* 纯工具与校验接口 */
uint32_t zvp_lut_crc32(const void *data, size_t len);
bool     zvp_lut_adjust_valid(zvp_lut_adjust_t a);
bool     zvp_lut_sharpness_valid(int value);
uint32_t zvp_lut_pack_adjust(zvp_lut_adjust_t a);
zvp_lut_adjust_t zvp_lut_unpack_adjust(uint32_t bits);

/* 上层生命周期与控制接口 */
void zvp_lut_init(zvp_lut_ctx_t *ctx, const zvp_lut_platform_ops_t *ops);
int  zvp_lut_load_bundle_and_cfg(zvp_lut_ctx_t *ctx);
int  zvp_lut_boot_activate(zvp_lut_ctx_t *ctx);
int  zvp_lut_ensure_kernel_ready(zvp_lut_ctx_t *ctx);
int  zvp_lut_select_slot(zvp_lut_ctx_t *ctx, uint32_t sel, bool persist_cfg, bool refresh_ui_preview);
int  zvp_lut_step_slot(zvp_lut_ctx_t *ctx, int delta, bool persist_cfg);
int  zvp_lut_toggle_enable(zvp_lut_ctx_t *ctx, uint32_t *io_last_nonzero_sel, bool persist_cfg);
int  zvp_lut_force_refresh(zvp_lut_ctx_t *ctx);
int  zvp_lut_reset_current_adjust(zvp_lut_ctx_t *ctx, bool persist_cfg);
bool zvp_lut_slot_is_modified(const zvp_lut_ctx_t *ctx, uint32_t sel);
int  zvp_lut_begin_fine_tune(zvp_lut_ctx_t *ctx, uint32_t sel);
int  zvp_lut_adjust_param_step(zvp_lut_ctx_t *ctx, zvp_lut_param_t param, int delta);
int  zvp_lut_reset_fine_tune_draft(zvp_lut_ctx_t *ctx);
int  zvp_lut_commit_fine_tune(zvp_lut_ctx_t *ctx);
int  zvp_lut_cancel_fine_tune(zvp_lut_ctx_t *ctx);
int  zvp_lut_flush_pending_cfg(zvp_lut_ctx_t *ctx);
int  zvp_lut_shutdown_reset(zvp_lut_ctx_t *ctx);

/* 阶段 3：SD 卡 ZVLOOK01 (.bin) 扫盘校验与在线热导入/恢复默认 */
int  zvp_lut_validate_bundle(const uint8_t *bytes, size_t size, uint32_t *out_count, uint32_t *out_digest);
int  zvp_lut_scan_sd_bundles(zvp_lut_ctx_t *ctx,
                             zvp_lut_sd_item_t *out_items,
                             uint32_t max_items,
                             uint32_t *out_count);
int  zvp_lut_hot_import_bytes(zvp_lut_ctx_t *ctx,
                              const uint8_t *bytes,
                              size_t size,
                              int32_t preferred_sel,
                              bool is_embedded);
int  zvp_lut_hot_import_item(zvp_lut_ctx_t *ctx, const zvp_lut_sd_item_t *item);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_LUT_H */
