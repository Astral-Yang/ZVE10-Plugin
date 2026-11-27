#ifndef ZVP_CRAW_H
#define ZVP_CRAW_H

#include "zvp_craw_shm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Pure-RAM 2K 16:9 (1920x1088) 12-bit cRAW + 48kHz WAV 核心控制器
 * ========================================================================== */

#define ZVP_CRAW_DEF_WIDTH        1920u
#define ZVP_CRAW_DEF_HEIGHT       1088u
#define ZVP_CRAW_DEF_ENCTYPE      2u
#define ZVP_CRAW_DEF_BPP          12u
#define ZVP_CRAW_PAGE_ALIGN       4096u
#define ZVP_CRAW_DIRECT_ALIGN     131072u /* exFAT 典型簇对齐粒度 128 KiB */
#define ZVP_CRAW_MAX_DMM_BLOCKS   7u      /* 阶段 2 实测并发上限：0x3c2 + 0x81227..0x8122c (38.5 MiB) */
#define ZVP_CRAW_FILE_HDR_MAGIC   0x3152435Au /* "ZCR1" */
#define ZVP_CRAW_FILE_HDR_BYTES   4096u   /* 4KB 对齐容器头，便于零拷贝直接拼接 */

typedef struct zvp_craw_extent {
    uint32_t paddr;
    uint32_t size;
} zvp_craw_extent_t;

typedef struct zvp_craw_dmm_block {
    uint32_t unit_type;
    uint16_t unit_handle;
    uint32_t paddr;
    uint32_t size;
    bool held;
} zvp_craw_dmm_block_t;

/* .RAW 容器文件 4KB 头部前 64 字节固定元数据 */
typedef struct zvp_craw_file_hdr {
    uint32_t magic;       /* ZVP_CRAW_FILE_HDR_MAGIC ("ZCR1") */
    uint32_t version;     /* 1 */
    uint32_t width;       /* 1920 */
    uint32_t height;      /* 1088 */
    uint32_t bpp;         /* 12 */
    uint32_t enc_type;    /* 2 */
    uint32_t fps_num;     /* 24000 */
    uint32_t fps_den;     /* 1001 */
    uint32_t frame_bytes; /* 2088960 (510 * 4096) */
    uint32_t frame_count;
    uint32_t drop_count;
    uint32_t flags;
    uint32_t reserved[4];
} zvp_craw_file_hdr_t;

typedef struct zvp_craw_platform_ops {
    int  (*phys_read)(uint32_t pa, void *out_buf, size_t len);
    int  (*phys_write)(uint32_t pa, const void *buf, size_t len);
    int  (*dmm_alloc)(uint32_t unit_type, uint16_t *out_uhdl, uint32_t *out_pa, uint32_t *out_sz);
    int  (*dmm_free)(uint16_t uhdl);
    int  (*fsys_open_direct)(const char *path, int *out_fh);
    int  (*fsys_write_direct)(int fh, uint32_t align_sz,
                              const zvp_craw_extent_t *exts, uint32_t count,
                              uint32_t total_bytes);
    int  (*fsys_close)(int fh);
    int  (*fsys_seek)(int fh, uint64_t offset, int whence);
    int  (*fsys_write_file_direct)(const char *path, uint32_t total_bytes,
                                   const zvp_craw_extent_t *exts, uint32_t count);
    int  (*fsys_get_free_mb)(uint32_t *out_free_mb);
    uint64_t (*mono_ms)(void);
    void (*log_msg)(const char *msg);
} zvp_craw_platform_ops_t;

typedef struct zvp_craw_ctx {
    bool initialized;
    zvp_craw_liro_mode_t mode;
    bool liro_installed;
    bool pool_allocated;
    uint32_t width;
    uint32_t height;
    uint32_t enc_type;
    uint32_t frame_bytes;
    uint32_t total_slots;

    zvp_craw_dmm_block_t blocks[ZVP_CRAW_MAX_DMM_BLOCKS];
    uint32_t block_count;
    uint32_t pool_total_bytes;

    uint32_t clip_seq;
    uint32_t frames_written;
    uint32_t free_sd_mb;
    uint64_t last_poll_ms;
    int last_err;
    char status_msg[128];

    zvp_craw_shm_t shm_snap;
    zvp_craw_platform_ops_t ops;

    /* 连续录像后台排空状态机 */
    bool drainer_running;
    volatile bool drainer_stop;
    uint64_t drainer_tid;
    bool rec_clip_open;
    bool wav_first_flushed;
    int raw_fh;
    int wav_fh;
    uint32_t clip_frames;
    uint32_t clip_drops;
    uint32_t clip_audio_bytes;
    uint32_t audio_ring_base;
    uint32_t audio_ring_size;
    uint32_t audio_last_wp;
    uint32_t stage_pa;
    uint32_t stage_bytes;
    uint32_t wav_stage_used;
    uint8_t  wav_first_page[ZVP_CRAW_FILE_HDR_BYTES];
    uint8_t  scratch_page[ZVP_CRAW_FILE_HDR_BYTES];
    uint32_t diag_last_rec_state;
    uint32_t diag_rec_ticks;
} zvp_craw_ctx_t;

/* 纯逻辑 / 封装计算函数 */
uint32_t zvp_craw_calc_frame_bytes(uint32_t width, uint32_t height, uint32_t enc_type);
void zvp_craw_build_wav_header(uint8_t out44[44], uint32_t sample_rate,
                               uint16_t channels, uint16_t bits_per_sample,
                               uint32_t pcm_data_bytes);
void zvp_craw_build_raw_header(uint8_t *out_page, size_t cap,
                               const zvp_craw_file_hdr_t *hdr);
int zvp_craw_format_paths(uint32_t clip_idx,
                          char *out_raw, size_t raw_cap,
                          char *out_wav, size_t wav_cap);

/* 控制器生命周期、LIRO 热装载/卸载、DMM 池管理与零拷贝快照写卡 */
void zvp_craw_init(zvp_craw_ctx_t *ctx, const zvp_craw_platform_ops_t *ops);
int zvp_craw_alloc_pool(zvp_craw_ctx_t *ctx);
void zvp_craw_free_pool(zvp_craw_ctx_t *ctx);
int zvp_craw_install_liro(zvp_craw_ctx_t *ctx);
int zvp_craw_uninstall_liro(zvp_craw_ctx_t *ctx);
int zvp_craw_set_mode(zvp_craw_ctx_t *ctx, zvp_craw_liro_mode_t mode);
int zvp_craw_cycle_mode(zvp_craw_ctx_t *ctx);
int zvp_craw_poll_telemetry(zvp_craw_ctx_t *ctx, uint64_t now_ms);
int zvp_craw_drainer_step(zvp_craw_ctx_t *ctx);
int zvp_craw_dump_test_snapshot(zvp_craw_ctx_t *ctx);
void zvp_craw_shutdown(zvp_craw_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_CRAW_H */
