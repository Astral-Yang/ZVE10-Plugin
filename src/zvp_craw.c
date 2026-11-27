#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "zvp_craw.h"
#include "zvp_liro_blob.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(ZVP_HOST_TEST)
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#endif

/* 主缓冲池候选表（按优先级非阻塞探测，优先 0x3c2，不可用则切至已空闲的 Standby 分区） */
static const uint32_t k_dmm_primary_uts[] = {
    0x0003C2u, /* Movie 模式下为 28.00 MiB @ 0x1a000000；Standby 下若被占用会 <1ms 返回 */
    0x000307u, /* 0x24000000 根分区 100.00 MiB（不占用 0x1a000000） */
    0x000308u, /* 0x24000000 根分区备用 100.00 MiB */
    0x000288u, /* 0x1a000000 当前活跃 tbl[250] 50.00 MiB */
    0x0003C8u  /* 0x1a000000 当前活跃 tbl[250] 15.00 MiB */
};

/* 次级 :uc 扩展池候选表 */
static const uint32_t k_dmm_secondary_uts[ZVP_CRAW_MAX_DMM_BLOCKS - 1u] = {
    0x081227u, /* 0.50 MiB */
    0x081228u, /* 1.00 MiB */
    0x081229u, /* 1.50 MiB */
    0x08122Au, /* 2.00 MiB */
    0x08122Bu, /* 2.50 MiB */
    0x08122Cu  /* 3.00 MiB */
};

typedef struct hook_slot_desc {
    uint32_t slot_vma;
    uint32_t expected_orig;
    uint32_t tbl_index;
    uint32_t hook_bit;
    size_t   shm_backup_off;
} hook_slot_desc_t;

static const hook_slot_desc_t k_hook_slots[] = {
    {
        ZVP_AVCAM_SLOT_SEQ_MOVIE, ZVP_AVCAM_ORIG_SEQ_MOVIE,
        ZVP_LIRO_TBL_SEQ_MOVIE, ZVP_CRAW_HOOK_BIT_SEQ,
        offsetof(zvp_craw_shm_t, orig_seq_movie)
    },
    {
        ZVP_AVCAM_SLOT_RDCW0_S7, ZVP_AVCAM_ORIG_RDCW0_S7,
        ZVP_LIRO_TBL_RDCW0_S7, ZVP_CRAW_HOOK_BIT_RDCW0_S7,
        offsetof(zvp_craw_shm_t, orig_rdcw0_s7)
    },
    {
        ZVP_AVCAM_SLOT_CAMRDC_CFG, ZVP_AVCAM_ORIG_CAMRDC_CFG,
        ZVP_LIRO_TBL_CAMRDC_CFG, ZVP_CRAW_HOOK_BIT_RDC_CFG,
        offsetof(zvp_craw_shm_t, orig_camrdc_cfg)
    },
    {
        ZVP_AVCAM_SLOT_CAMAIF_S4, ZVP_AVCAM_ORIG_CAMAIF_S4,
        ZVP_LIRO_TBL_CAMAIF_S4, ZVP_CRAW_HOOK_BIT_CAMAIF_S4,
        offsetof(zvp_craw_shm_t, orig_camaif_s4)
    },
    {
        ZVP_AVCAM_SLOT_IFMT_S4, ZVP_AVCAM_ORIG_IFMT_S4,
        ZVP_LIRO_TBL_IFMT_S4, ZVP_CRAW_HOOK_BIT_IFMT_S4,
        offsetof(zvp_craw_shm_t, orig_ifmt_s4)
    },
    {
        ZVP_AVCAM_SLOT_IFMT_S10, ZVP_AVCAM_ORIG_IFMT_S10,
        ZVP_LIRO_TBL_IFMT_S10, ZVP_CRAW_HOOK_BIT_IFMT_S10,
        offsetof(zvp_craw_shm_t, orig_ifmt_s10)
    },
    {
        ZVP_AVCAM_SLOT_DFE_RDC_S10, ZVP_AVCAM_ORIG_DFE_RDC_S10,
        ZVP_LIRO_TBL_DFE_RDC_S10, ZVP_CRAW_HOOK_BIT_DFE_RDC,
        offsetof(zvp_craw_shm_t, orig_dfe_rdc_s10)
    },
    {
        ZVP_AVCAM_SLOT_DFE_RDC_CFG, ZVP_AVCAM_ORIG_DFE_RDC_CFG,
        ZVP_LIRO_TBL_DFE_RDC_CFG, ZVP_CRAW_HOOK_BIT_DFE_RDC2,
        offsetof(zvp_craw_shm_t, orig_dfe_rdc_cfg)
    },
    {
        ZVP_AVCAM_SLOT_DFE_MAIN, ZVP_AVCAM_ORIG_DFE_MAIN,
        ZVP_LIRO_TBL_DFE_MAIN, ZVP_CRAW_HOOK_BIT_DFE_MAIN,
        offsetof(zvp_craw_shm_t, orig_dfe_main)
    },
    {
        ZVP_AVCAM_SLOT_STATE_HDL, ZVP_AVCAM_ORIG_STATE_HDL,
        ZVP_LIRO_TBL_STATE_HDL, ZVP_CRAW_HOOK_BIT_STATE,
        offsetof(zvp_craw_shm_t, orig_state_hdl)
    },
    {
        ZVP_AVCAM_SLOT_DFE_RDCW0, ZVP_AVCAM_ORIG_DFE_RDCW0,
        ZVP_LIRO_TBL_DFE_RDCW0, ZVP_CRAW_HOOK_BIT_DFE_RDCW0,
        offsetof(zvp_craw_shm_t, orig_dfe_rdcw0)
    },
};

typedef struct {
    uint32_t slot_vma;
    uint32_t expected_orig;
    uint32_t ext_index;
} ext_hook_slot_desc_t;

static const ext_hook_slot_desc_t k_ext_hook_slots[ZVP_LIRO_EXT_COUNT] = {
    { ZVP_AVCAM_EXT_RDCW1_S7,   ZVP_AVCAM_EXT_ORIG_RDCW1_S7,   ZVP_LIRO_EXT_RDCW1_S7 },
    { ZVP_AVCAM_EXT_RDCW1_S8,   ZVP_AVCAM_EXT_ORIG_RDCW1_S8,   ZVP_LIRO_EXT_RDCW1_S8 },
    { ZVP_AVCAM_EXT_SPL_EE_S2,  ZVP_AVCAM_EXT_ORIG_SPL_EE_S2,  ZVP_LIRO_EXT_SPL_EE_S2 },
    { ZVP_AVCAM_EXT_EE_S2,      ZVP_AVCAM_EXT_ORIG_EE_S2,      ZVP_LIRO_EXT_EE_S2 },
    { ZVP_AVCAM_EXT_SPL_EE_S17, ZVP_AVCAM_EXT_ORIG_SPL_EE_S17, ZVP_LIRO_EXT_SPL_EE_S17 },
    { ZVP_AVCAM_EXT_EE_S17,     ZVP_AVCAM_EXT_ORIG_EE_S17,     ZVP_LIRO_EXT_EE_S17 },
    { ZVP_AVCAM_EXT_SPL_EE_S22, ZVP_AVCAM_EXT_ORIG_SPL_EE_S22, ZVP_LIRO_EXT_SPL_EE_S22 },
    { ZVP_AVCAM_EXT_SPL_CAP_S23,ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23,ZVP_LIRO_EXT_SPL_CAP_S23 },
};

static void craw_log_v18_summary(zvp_craw_ctx_t *ctx, const char *tag);

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void craw_logf(const zvp_craw_ctx_t *ctx, const char *fmt, ...) {
    if (!ctx || !ctx->ops.log_msg || !fmt) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ctx->ops.log_msg(buf);
}

static void set_status(zvp_craw_ctx_t *ctx, int err, const char *fmt_msg) {
    ctx->last_err = err;
    if (fmt_msg) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg), "%s", fmt_msg);
    }
}

uint32_t zvp_craw_calc_frame_bytes(uint32_t width, uint32_t height, uint32_t enc_type) {
    if (width == 0u || height == 0u) return 0u;
    /* encType=2 为 Sony cRAW ~8-bit/px 压缩载荷；其余按 12-bit packed 估算，均 4KB 页对齐 */
    uint64_t raw = (enc_type == ZVP_CRAW_DEF_ENCTYPE)
                       ? ((uint64_t)width * (uint64_t)height)
                       : (((uint64_t)width * (uint64_t)height * 3ull + 1ull) / 2ull);
    uint64_t aligned = (raw + ZVP_CRAW_PAGE_ALIGN - 1ull) & ~(uint64_t)(ZVP_CRAW_PAGE_ALIGN - 1ull);
    return (uint32_t)aligned;
}

void zvp_craw_build_wav_header(uint8_t out44[44], uint32_t sample_rate,
                               uint16_t channels, uint16_t bits_per_sample,
                               uint32_t pcm_data_bytes) {
    if (!out44) return;
    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8u));
    uint32_t byte_rate = sample_rate * (uint32_t)block_align;
    uint32_t riff_size = 36u + pcm_data_bytes;

    memcpy(out44 + 0, "RIFF", 4);
    put_le32(out44 + 4, riff_size);
    memcpy(out44 + 8, "WAVE", 4);
    memcpy(out44 + 12, "fmt ", 4);
    put_le32(out44 + 16, 16u);
    put_le16(out44 + 20, 1u); /* PCM */
    put_le16(out44 + 22, channels);
    put_le32(out44 + 24, sample_rate);
    put_le32(out44 + 28, byte_rate);
    put_le16(out44 + 32, block_align);
    put_le16(out44 + 34, bits_per_sample);
    memcpy(out44 + 36, "data", 4);
    put_le32(out44 + 40, pcm_data_bytes);
}

void zvp_craw_build_raw_header(uint8_t *out_page, size_t cap,
                               const zvp_craw_file_hdr_t *hdr) {
    if (!out_page || !hdr || cap < sizeof(*hdr)) return;
    memset(out_page, 0, cap);
    memcpy(out_page, hdr, sizeof(*hdr));
}

int zvp_craw_format_paths(uint32_t clip_idx,
                          char *out_raw, size_t raw_cap,
                          char *out_wav, size_t wav_cap) {
    uint32_t id = clip_idx % 100000u; /* 严格满足 8.3 短文件名: "ZVC00001.RAW" */
    if (out_raw && raw_cap >= 16u) {
        snprintf(out_raw, raw_cap, "A:/ZVC%05u.RAW", id);
    }
    if (out_wav && wav_cap >= 16u) {
        snprintf(out_wav, wav_cap, "A:/ZVC%05u.WAV", id);
    }
    return 0;
}

void zvp_craw_init(zvp_craw_ctx_t *ctx, const zvp_craw_platform_ops_t *ops) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    if (ops) ctx->ops = *ops;
    ctx->width = ZVP_CRAW_DEF_WIDTH;
    ctx->height = ZVP_CRAW_DEF_HEIGHT;
    ctx->enc_type = ZVP_CRAW_DEF_ENCTYPE;
    ctx->frame_bytes = zvp_craw_calc_frame_bytes(ctx->width, ctx->height, ctx->enc_type);
    ctx->clip_seq = 1u;
    ctx->raw_fh = -1;
    ctx->wav_fh = -1;
    ctx->initialized = true;
    set_status(ctx, 0, "未开启 (安全旁路)");
    craw_logf(ctx, "[craw_boot] build=v33");
}

int zvp_craw_alloc_pool(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->ops.dmm_alloc || !ctx->ops.dmm_free) return -1;
    if (ctx->pool_allocated) return 0;

    uint32_t sum = 0u;
    uint32_t cnt = 0u;
    const uint32_t min_primary = (ctx->frame_bytes > 0u)
                                 ? (ctx->frame_bytes * 2u)
                                 : (2u * ZVP_CRAW_FILE_HDR_BYTES);

    /* 阶段 1：非阻塞按优先级尝试主环形池 */
    int last_rc = -2;
    for (size_t i = 0; i < sizeof(k_dmm_primary_uts) / sizeof(k_dmm_primary_uts[0]); ++i) {
        uint16_t uh = 0;
        uint32_t pa = 0;
        uint32_t sz = 0;
        uint32_t ut = k_dmm_primary_uts[i];
        craw_logf(ctx, "[craw] dmm_alloc primary try i=%u ut=0x%x", (unsigned)i, (unsigned)ut);
        int rc = ctx->ops.dmm_alloc(ut, &uh, &pa, &sz);
        craw_logf(ctx, "[craw] dmm_alloc primary res ut=0x%x rc=%d uh=0x%x pa=0x%08x sz=0x%x",
                  (unsigned)ut, rc, (unsigned)uh, (unsigned)pa, (unsigned)sz);
        if (rc == 0 && pa != 0u && sz >= min_primary) {
            ctx->blocks[0].unit_type = ut;
            ctx->blocks[0].unit_handle = uh;
            ctx->blocks[0].paddr = pa;
            ctx->blocks[0].size = sz;
            ctx->blocks[0].held = true;
            sum = sz;
            cnt = 1u;
            break;
        }
        if (rc == 0 && uh != 0u) {
            (void)ctx->ops.dmm_free(uh);
        }
        if (rc != 0) last_rc = rc;
    }

    if (cnt == 0u) {
        set_status(ctx, last_rc ? last_rc : -2, "DMM 主池无可用块");
        return -2;
    }

    /* 阶段 2：非阻塞探测次级 :uc 扩展池（如某块繁忙则跳过继续，不卡死） */
    for (size_t i = 0; i < sizeof(k_dmm_secondary_uts) / sizeof(k_dmm_secondary_uts[0]); ++i) {
        if (cnt >= ZVP_CRAW_MAX_DMM_BLOCKS) break;
        uint16_t uh = 0;
        uint32_t pa = 0;
        uint32_t sz = 0;
        uint32_t ut = k_dmm_secondary_uts[i];
        int rc = ctx->ops.dmm_alloc(ut, &uh, &pa, &sz);
        craw_logf(ctx, "[craw] dmm_alloc sec ut=0x%x rc=%d uh=0x%x pa=0x%08x sz=0x%x",
                  (unsigned)ut, rc, (unsigned)uh, (unsigned)pa, (unsigned)sz);
        if (rc != 0 || sz == 0u || pa == 0u) {
            if (rc == 0 && uh != 0u) {
                (void)ctx->ops.dmm_free(uh);
            }
            continue;
        }
        ctx->blocks[cnt].unit_type = ut;
        ctx->blocks[cnt].unit_handle = uh;
        ctx->blocks[cnt].paddr = pa;
        ctx->blocks[cnt].size = sz;
        ctx->blocks[cnt].held = true;
        sum += sz;
        cnt++;
    }

    ctx->block_count = cnt;
    ctx->pool_total_bytes = sum;
    ctx->pool_allocated = (cnt > 0u);
    ctx->total_slots = ctx->frame_bytes ? (ctx->blocks[0].size / ctx->frame_bytes) : 0u;

    /* 更新 SHM 中的环形描述符（若已挂载） */
    if (ctx->liro_installed && ctx->ops.phys_write && ctx->pool_allocated) {
        ctx->shm_snap.ring_base_pa = ctx->blocks[0].paddr;
        ctx->shm_snap.ring_total_bytes = ctx->blocks[0].size;
        ctx->shm_snap.frame_stride = ctx->frame_bytes;
        ctx->shm_snap.slot_count = ctx->total_slots;
        ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, ring_base_pa),
                            &ctx->shm_snap.ring_base_pa, sizeof(uint32_t) * 4u);
    }

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             "DMM %.1f MiB (%u 块 / %u 帧槽)",
             (double)sum / 1048576.0, cnt, ctx->total_slots);
    ctx->last_err = 0;
    return 0;
}

void zvp_craw_free_pool(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->ops.dmm_free) return;
    for (uint32_t i = 0; i < ctx->block_count; ++i) {
        if (ctx->blocks[i].held) {
            ctx->ops.dmm_free(ctx->blocks[i].unit_handle);
            ctx->blocks[i].held = false;
        }
    }
    ctx->block_count = 0u;
    ctx->pool_total_bytes = 0u;
    ctx->total_slots = 0u;
    ctx->stage_pa = 0u;
    ctx->stage_bytes = 0u;
    ctx->pool_allocated = false;
}

static bool is_our_tail_ptr(uint32_t val) {
    return (val >= ZVP_CRAW_CODE_VMA && val < (ZVP_CRAW_CODE_VMA + ZVP_CRAW_CODE_MAX_SIZE));
}

int zvp_craw_install_liro(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->ops.phys_read || !ctx->ops.phys_write) return -1;
    if (ctx->liro_installed) return 0;

    zvp_craw_shm_t shm;
    memset(&shm, 0, sizeof(shm));

    /* 1. 先检查 av-cam.bin 6 个 .data VTable 槽位的真实当前状态 */
    uint32_t cur_slots[sizeof(k_hook_slots) / sizeof(k_hook_slots[0])];
    bool any_hooked = false;
    for (size_t i = 0; i < sizeof(k_hook_slots) / sizeof(k_hook_slots[0]); ++i) {
        uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_hook_slots[i].slot_vma);
        if (ctx->ops.phys_read(pa, &cur_slots[i], sizeof(cur_slots[i])) != 0) {
            set_status(ctx, -5, "读取 av-cam VTable 失败");
            return -5;
        }
        if (is_our_tail_ptr(cur_slots[i])) {
            any_hooked = true;
        } else if (cur_slots[i] != k_hook_slots[i].expected_orig) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     "向量偏移不匹配 0x%08x=0x%08x", k_hook_slots[i].slot_vma, cur_slots[i]);
            ctx->last_err = -6;
            return -6;
        }
    }

    uint32_t cur_ext[ZVP_LIRO_EXT_COUNT];
    for (size_t i = 0; i < ZVP_LIRO_EXT_COUNT; ++i) {
        uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_ext_hook_slots[i].slot_vma);
        if (ctx->ops.phys_read(pa, &cur_ext[i], sizeof(cur_ext[i])) != 0) {
            set_status(ctx, -5, "读取 av-cam 扩展 VTable 失败");
            return -5;
        }
        if (is_our_tail_ptr(cur_ext[i])) {
            any_hooked = true;
        } else if (cur_ext[i] != k_ext_hook_slots[i].expected_orig) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     "扩展向量偏移不匹配 0x%08x=0x%08x",
                     k_ext_hook_slots[i].slot_vma, cur_ext[i]);
            ctx->last_err = -6;
            return -6;
        }
    }

    uint32_t prev_dbg_magic = 0u;
    uint32_t prev_ext_orig[ZVP_LIRO_EXT_COUNT];
    memset(prev_ext_orig, 0, sizeof(prev_ext_orig));
    (void)ctx->ops.phys_read(ZVP_CRAW_DBG_PA, &prev_dbg_magic, sizeof(prev_dbg_magic));
    if (prev_dbg_magic == ZVP_CRAW_DBG_MAGIC) {
        (void)ctx->ops.phys_read(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, v18_ext_orig),
                                 prev_ext_orig, sizeof(prev_ext_orig));
    }

    /* 2. 分块检验 PA 0x0471AD00 尾部机器码是否完整无损（避免 UI 回调栈上分配数 KB 大数组） */
    uint8_t chunk[256];
    bool code_intact = true;
    for (uint32_t off = 0u; off < ZVP_LIRO_BLOB_SIZE; off += (uint32_t)sizeof(chunk)) {
        uint32_t take = ZVP_LIRO_BLOB_SIZE - off;
        if (take > (uint32_t)sizeof(chunk)) take = (uint32_t)sizeof(chunk);
        if (ctx->ops.phys_read(ZVP_CRAW_CODE_PA + off, chunk, take) != 0 ||
            memcmp(chunk, zvp_liro_blob_bytes + off, take) != 0) {
            code_intact = false;
            break;
        }
    }

    uint32_t existing_magic = 0u;
    if (ctx->ops.phys_read(ZVP_CRAW_SHM_PA, &existing_magic, sizeof(existing_magic)) != 0) {
        set_status(ctx, -3, "/dev/mem 读共享区失败");
        return -3;
    }

    /* 仅当当前进程周期内已经挂钩且机器码完整、SHM 有效时复用既有 SHM */
    if (any_hooked && code_intact && existing_magic == ZVP_CRAW_SHM_MAGIC) {
        if (ctx->ops.phys_read(ZVP_CRAW_SHM_PA, &shm, sizeof(shm)) != 0) {
            set_status(ctx, -4, "读取已有 SHM 失败");
            return -4;
        }
    } else {
        /* 首次安装或检测到热重启 DRAM 残留：强制重新备份原厂地址、清零 SHM 并刷写尾洞代码 */
        for (size_t i = 0; i < sizeof(k_hook_slots) / sizeof(k_hook_slots[0]); ++i) {
            uint32_t backup_val = is_our_tail_ptr(cur_slots[i])
                                      ? k_hook_slots[i].expected_orig
                                      : cur_slots[i];
            memcpy((uint8_t *)&shm + k_hook_slots[i].shm_backup_off, &backup_val, sizeof(uint32_t));
        }

        if (ctx->ops.phys_write(ZVP_CRAW_CODE_PA, zvp_liro_blob_bytes, ZVP_LIRO_BLOB_SIZE) != 0) {
            set_status(ctx, -7, "写入 LIRO 尾洞代码失败");
            return -7;
        }
        for (uint32_t off = 0u; off < ZVP_LIRO_BLOB_SIZE; off += (uint32_t)sizeof(chunk)) {
            uint32_t take = ZVP_LIRO_BLOB_SIZE - off;
            if (take > (uint32_t)sizeof(chunk)) take = (uint32_t)sizeof(chunk);
            memset(chunk, 0, take);
            if (ctx->ops.phys_read(ZVP_CRAW_CODE_PA + off, chunk, take) != 0 ||
                memcmp(chunk, zvp_liro_blob_bytes + off, take) != 0) {
                set_status(ctx, -7, "回读校验 LIRO 尾洞代码失败");
                return -7;
            }
        }
        /* 分块清零 4KB 扩展调试区并写入 magic */
        for (uint32_t off = 0u; off < (uint32_t)sizeof(zvp_craw_dbg_t); off += (uint32_t)sizeof(chunk)) {
            uint32_t take = (uint32_t)sizeof(zvp_craw_dbg_t) - off;
            if (take > (uint32_t)sizeof(chunk)) take = (uint32_t)sizeof(chunk);
            memset(chunk, 0, take);
            if (off == 0u) {
                uint32_t magic = ZVP_CRAW_DBG_MAGIC;
                memcpy(chunk, &magic, sizeof(magic));
            }
            (void)ctx->ops.phys_write(ZVP_CRAW_DBG_PA + off, chunk, take);
        }
    }

    /* 2. 写入或更新 SHM 头部与控制参数 */
    shm.magic = ZVP_CRAW_SHM_MAGIC;
    shm.version = ZVP_CRAW_SHM_VERSION;
    shm.mode = (uint32_t)ctx->mode;
    shm.flags = ZVP_CRAW_FLAG_OVERRIDE_RDC | ZVP_CRAW_FLAG_COUNT_ISR;
    shm.target_enc_type = ctx->enc_type;
    shm.target_w = ctx->width;
    shm.target_h = ctx->height;
    shm.payload_bytes = ZVP_LIRO_BLOB_SIZE;
    if (ctx->pool_allocated && ctx->block_count > 0u) {
        shm.ring_base_pa = ctx->blocks[0].paddr;
        shm.ring_total_bytes = ctx->blocks[0].size;
        shm.frame_stride = ctx->frame_bytes;
        shm.slot_count = ctx->total_slots;
    }
    if (ctx->ops.phys_write(ZVP_CRAW_SHM_PA, &shm, sizeof(shm)) != 0) {
        set_status(ctx, -8, "写入 SHM 控制块失败");
        return -8;
    }

    /* 3. 读取尾洞头部跳转表，替换 6 个 .data VTable/全局槽位 */
    zvp_liro_dispatch_header_t hdr;
    if (ctx->ops.phys_read(ZVP_CRAW_CODE_PA, &hdr, sizeof(hdr)) != 0 ||
        hdr.magic != ZVP_CRAW_SHM_MAGIC) {
        set_status(ctx, -9, "校验 LIRO 头部魔数失败");
        return -9;
    }

    uint32_t mask = 0u;
    for (size_t i = 0; i < sizeof(k_hook_slots) / sizeof(k_hook_slots[0]); ++i) {
        uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_hook_slots[i].slot_vma);
        uint32_t target = hdr.entries[k_hook_slots[i].tbl_index];
        if (ctx->ops.phys_write(pa, &target, sizeof(target)) == 0) {
            mask |= k_hook_slots[i].hook_bit;
        }
    }
    shm.hook_installed_mask = mask;
    ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, hook_installed_mask),
                        &mask, sizeof(mask));

    uint32_t ext_orig[ZVP_LIRO_EXT_COUNT];
    uint32_t ext_mask = 0u;
    for (size_t i = 0; i < ZVP_LIRO_EXT_COUNT; ++i) {
        ext_orig[i] = is_our_tail_ptr(cur_ext[i])
            ? (prev_ext_orig[i] != 0u ? prev_ext_orig[i] : k_ext_hook_slots[i].expected_orig)
            : cur_ext[i];
    }
    (void)ctx->ops.phys_write(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, v18_ext_orig),
                              ext_orig, sizeof(ext_orig));
    for (size_t i = 0; i < ZVP_LIRO_EXT_COUNT; ++i) {
        uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_ext_hook_slots[i].slot_vma);
        uint32_t target = hdr.ext_entries[k_ext_hook_slots[i].ext_index];
        if (ctx->ops.phys_write(pa, &target, sizeof(target)) == 0) {
            ext_mask |= (1u << i);
        }
    }
    (void)ctx->ops.phys_write(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, v18_ext_installed_mask),
                              &ext_mask, sizeof(ext_mask));

    ctx->shm_snap = shm;
    ctx->liro_installed = (mask != 0u);
    set_status(ctx, 0, "LIRO 尾洞已挂钩");
    return 0;
}

int zvp_craw_uninstall_liro(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->ops.phys_read || !ctx->ops.phys_write) return -1;

    uint32_t mode_zero = (uint32_t)ZVP_CRAW_LIRO_BYPASS;
    (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, mode),
                              &mode_zero, sizeof(mode_zero));

    zvp_craw_shm_t shm;
    memset(&shm, 0, sizeof(shm));
    if (ctx->ops.phys_read(ZVP_CRAW_SHM_PA, &shm, sizeof(shm)) == 0 &&
        shm.magic == ZVP_CRAW_SHM_MAGIC) {
        for (size_t i = 0; i < sizeof(k_hook_slots) / sizeof(k_hook_slots[0]); ++i) {
            uint32_t orig = 0u;
            memcpy(&orig, (const uint8_t *)&shm + k_hook_slots[i].shm_backup_off, sizeof(uint32_t));
            if (orig == 0u) orig = k_hook_slots[i].expected_orig;
            uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_hook_slots[i].slot_vma);
            (void)ctx->ops.phys_write(pa, &orig, sizeof(orig));
        }
        uint32_t zero_mask = 0u;
        (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, hook_installed_mask),
                                  &zero_mask, sizeof(zero_mask));
    }

    craw_log_v18_summary(ctx, "uninst");
    uint32_t dbg_magic = 0u;
    uint32_t ext_orig[ZVP_LIRO_EXT_COUNT];
    memset(ext_orig, 0, sizeof(ext_orig));
    if (ctx->ops.phys_read(ZVP_CRAW_DBG_PA, &dbg_magic, sizeof(dbg_magic)) == 0 &&
        dbg_magic == ZVP_CRAW_DBG_MAGIC) {
        (void)ctx->ops.phys_read(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, v18_ext_orig),
                                 ext_orig, sizeof(ext_orig));
    }
    for (size_t i = 0; i < ZVP_LIRO_EXT_COUNT; ++i) {
        uint32_t orig = ext_orig[i] != 0u ? ext_orig[i] : k_ext_hook_slots[i].expected_orig;
        uint32_t pa = ZVP_AVCAM_VMA_TO_PA(k_ext_hook_slots[i].slot_vma);
        (void)ctx->ops.phys_write(pa, &orig, sizeof(orig));
    }
    uint32_t ext_zero = 0u;
    (void)ctx->ops.phys_write(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, v18_ext_installed_mask),
                              &ext_zero, sizeof(ext_zero));

    ctx->liro_installed = false;
    ctx->mode = ZVP_CRAW_LIRO_BYPASS;
    set_status(ctx, 0, "已卸载 LIRO 钩子并恢复原厂");
    return 0;
}

static void stop_drainer_thread(zvp_craw_ctx_t *ctx);
static int start_drainer_thread(zvp_craw_ctx_t *ctx);

int zvp_craw_set_mode(zvp_craw_ctx_t *ctx, zvp_craw_liro_mode_t mode) {
    if (!ctx || !ctx->initialized) return -1;

    if (mode == ZVP_CRAW_LIRO_BYPASS) {
        stop_drainer_thread(ctx);
        ctx->mode = ZVP_CRAW_LIRO_BYPASS;
        if (ctx->liro_installed && ctx->ops.phys_write) {
            uint32_t m = (uint32_t)ZVP_CRAW_LIRO_BYPASS;
            ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, mode), &m, sizeof(m));
        }
        set_status(ctx, 0, "安全旁路 (Hooks 静默)");
        return 0;
    }

    if (mode != ZVP_CRAW_LIRO_ARM_2K && mode != ZVP_CRAW_LIRO_RAW_W0) {
        stop_drainer_thread(ctx);
    }

    if ((mode == ZVP_CRAW_LIRO_ARM_2K || mode == ZVP_CRAW_LIRO_RAW_W0) && !ctx->rec_clip_open) {
        uint32_t def_stride = zvp_craw_calc_frame_bytes(ctx->width, ctx->height, ctx->enc_type);
        uint32_t want_stride = ctx->frame_bytes;
        if (mode == ZVP_CRAW_LIRO_RAW_W0 && ctx->frame_bytes == def_stride) {
            want_stride = 4608u * 1024u; /* 4.5 MiB: 容纳 2008x1134x16-bit (4.34 MiB) 且保留 >=20 槽 */
        } else if (mode == ZVP_CRAW_LIRO_ARM_2K && ctx->mode == ZVP_CRAW_LIRO_RAW_W0) {
            want_stride = def_stride;
        }
        if (want_stride > 0u && ctx->frame_bytes != want_stride) {
            ctx->frame_bytes = want_stride;
            if (ctx->pool_allocated && ctx->block_count > 0u) {
                ctx->total_slots = ctx->blocks[0].size / want_stride;
                if (ctx->block_count < 2u && ctx->total_slots >= 3u) {
                    ctx->total_slots--;
                    ctx->stage_pa = ctx->blocks[0].paddr + ctx->total_slots * want_stride;
                    ctx->stage_bytes = want_stride & ~(ZVP_CRAW_PAGE_ALIGN - 1u);
                }
            }
        }
    }

    if ((mode == ZVP_CRAW_LIRO_ARM_2K || mode == ZVP_CRAW_LIRO_RAW_W0) && !ctx->pool_allocated) {
        craw_logf(ctx, "[craw] set_mode %u: alloc_pool", (unsigned)mode);
        int prc = zvp_craw_alloc_pool(ctx);
        craw_logf(ctx, "[craw] set_mode %u: alloc_pool rc=%d", (unsigned)mode, prc);
        if (prc != 0) return prc;
    }

    ctx->mode = mode;
    if (!ctx->liro_installed) {
        craw_logf(ctx, "[craw] set_mode: install_liro begin");
        int irc = zvp_craw_install_liro(ctx);
        craw_logf(ctx, "[craw] set_mode: install_liro rc=%d", irc);
        if (irc != 0) return irc;
    } else if (ctx->ops.phys_write) {
        if (ctx->pool_allocated && ctx->block_count > 0u) {
            ctx->shm_snap.frame_stride = ctx->frame_bytes;
            ctx->shm_snap.slot_count = ctx->total_slots;
            (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, frame_stride),
                                      &ctx->shm_snap.frame_stride, sizeof(uint32_t) * 2u);
        }
        uint32_t m = (uint32_t)mode;
        craw_logf(ctx, "[craw] set_mode: write SHM mode=%u stride=0x%x slots=%u",
                  (unsigned)m, (unsigned)ctx->frame_bytes, (unsigned)ctx->total_slots);
        ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, mode), &m, sizeof(m));
    }

    if (mode == ZVP_CRAW_LIRO_ARM_2K || mode == ZVP_CRAW_LIRO_RAW_W0) {
        craw_logf(ctx, "[craw] set_mode: start_drainer begin");
        int drc = start_drainer_thread(ctx);
        craw_logf(ctx, "[craw] set_mode: start_drainer rc=%d", drc);
    }

    if (ctx->ops.fsys_get_free_mb) {
        craw_logf(ctx, "[craw] set_mode: fsys_get_free_mb begin");
        (void)ctx->ops.fsys_get_free_mb(&ctx->free_sd_mb);
        craw_logf(ctx, "[craw] set_mode: fsys_get_free_mb mb=%u", (unsigned)ctx->free_sd_mb);
    }

    const char *msg = "2K 16:9 cRAW 就绪";
    if (mode == ZVP_CRAW_LIRO_SPY_ONLY) msg = "Spy 只读抓参激活";
    else if (mode == ZVP_CRAW_LIRO_RAW_W0) msg = "W0 真16-bit Bayer 就绪";
    set_status(ctx, 0, msg);
    return 0;
}

int zvp_craw_cycle_mode(zvp_craw_ctx_t *ctx) {
    if (!ctx) return -1;
    zvp_craw_liro_mode_t next = ZVP_CRAW_LIRO_BYPASS;
    if (ctx->mode == ZVP_CRAW_LIRO_BYPASS) next = ZVP_CRAW_LIRO_SPY_ONLY;
    else if (ctx->mode == ZVP_CRAW_LIRO_SPY_ONLY) next = ZVP_CRAW_LIRO_ARM_2K;
    else if (ctx->mode == ZVP_CRAW_LIRO_ARM_2K) next = ZVP_CRAW_LIRO_RAW_W0;
    else next = ZVP_CRAW_LIRO_BYPASS;
    return zvp_craw_set_mode(ctx, next);
}

int zvp_craw_poll_telemetry(zvp_craw_ctx_t *ctx, uint64_t now_ms) {
    if (!ctx || !ctx->initialized || !ctx->liro_installed || !ctx->ops.phys_read) return 0;
    if (ctx->last_poll_ms != 0u && now_ms > ctx->last_poll_ms && (now_ms - ctx->last_poll_ms) < 250u) {
        return 0;
    }
    ctx->last_poll_ms = now_ms;
    int rc = ctx->ops.phys_read(ZVP_CRAW_SHM_PA, &ctx->shm_snap, sizeof(ctx->shm_snap));
    return rc;
}

int zvp_craw_dump_test_snapshot(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->ops.fsys_open_direct || !ctx->ops.fsys_write_direct || !ctx->ops.fsys_close) {
        return -1;
    }
    if (!ctx->pool_allocated) {
        int prc = zvp_craw_alloc_pool(ctx);
        if (prc != 0) return prc;
    }
    if (ctx->block_count == 0u || ctx->blocks[0].size < ZVP_CRAW_DIRECT_ALIGN) {
        set_status(ctx, -10, "DMM 缓冲不可用");
        return -10;
    }

    /* 若可用第二块 (>=512KB)，借其头部 4KB 写容器头，否则直接在第 0 块末尾/临时写 1 个 8 MiB 块 */
    char raw_path[32], wav_path[32];
    zvp_craw_format_paths(ctx->clip_seq, raw_path, sizeof(raw_path), wav_path, sizeof(wav_path));
    (void)wav_path;

    /* 用前 8 MiB（131072 对齐）验证将当前物理缓冲直写成 8.3 文件 */
    uint32_t dump_bytes = 8u * 1048576u;
    if (ctx->blocks[0].size < dump_bytes) {
        dump_bytes = ctx->blocks[0].size & ~(ZVP_CRAW_DIRECT_ALIGN - 1u);
    }

    if (ctx->ops.phys_write && ctx->blocks[0].size >= ZVP_CRAW_FILE_HDR_BYTES) {
        zvp_craw_file_hdr_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = ZVP_CRAW_FILE_HDR_MAGIC;
        hdr.version = 1u;
        hdr.width = ctx->width;
        hdr.height = ctx->height;
        hdr.bpp = ZVP_CRAW_DEF_BPP;
        hdr.enc_type = ctx->enc_type;
        hdr.fps_num = 24000u;
        hdr.fps_den = 1001u;
        hdr.frame_bytes = ctx->frame_bytes;
        hdr.frame_count = dump_bytes / ctx->frame_bytes;
        hdr.drop_count = ctx->shm_snap.drop_seq;
        zvp_craw_build_raw_header(ctx->scratch_page, sizeof(ctx->scratch_page), &hdr);
        (void)ctx->ops.phys_write(ctx->blocks[0].paddr, ctx->scratch_page, sizeof(ctx->scratch_page));
    }

    int fh = -1;
    int orc = ctx->ops.fsys_open_direct(raw_path, &fh);
    if (orc != 0 || fh < 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg), "Fsys_Open 失败 0x%08x", (unsigned)orc);
        ctx->last_err = orc;
        return orc ? orc : -11;
    }

    zvp_craw_extent_t ext;
    ext.paddr = ctx->blocks[0].paddr;
    ext.size = dump_bytes;
    int wrc = ctx->ops.fsys_write_direct(fh, ZVP_CRAW_DIRECT_ALIGN, &ext, 1u, dump_bytes);
    (void)ctx->ops.fsys_close(fh);

    if (wrc != (int)dump_bytes) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg), "直写异常 0x%08x", (unsigned)wrc);
        ctx->last_err = wrc;
        return -12;
    }

    /* 同步生成对应的 48kHz 16-bit 2ch 4KB 对齐 .WAV 占位/伴音头 */
    if (ctx->block_count >= 2u && ctx->ops.phys_write && ctx->ops.fsys_write_file_direct) {
        memset(ctx->scratch_page, 0, sizeof(ctx->scratch_page));
        zvp_craw_build_wav_header(ctx->scratch_page, 48000u, 2u, 16u, 4096u - 44u);
        if (ctx->ops.phys_write(ctx->blocks[1].paddr, ctx->scratch_page, sizeof(ctx->scratch_page)) == 0) {
            zvp_craw_extent_t wext = { ctx->blocks[1].paddr, 4096u };
            (void)ctx->ops.fsys_write_file_direct(wav_path, 4096u, &wext, 1u);
        }
    }

    ctx->frames_written += (dump_bytes / ctx->frame_bytes);
    ctx->clip_seq++;
    if (ctx->ops.fsys_get_free_mb) {
        (void)ctx->ops.fsys_get_free_mb(&ctx->free_sd_mb);
    }
    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             "已快照 %s (%u MiB)", raw_path, dump_bytes / 1048576u);
    ctx->last_err = 0;
    return 0;
}

static int craw_ensure_stage_buffer(zvp_craw_ctx_t *ctx) {
    if (ctx->stage_pa != 0u && ctx->stage_bytes >= 2u * ZVP_CRAW_FILE_HDR_BYTES) {
        return 0;
    }
    if (!ctx->pool_allocated || ctx->block_count == 0u) {
        return -1;
    }
    if (ctx->block_count >= 2u && ctx->blocks[1].size >= 2u * ZVP_CRAW_FILE_HDR_BYTES) {
        ctx->stage_pa = ctx->blocks[1].paddr;
        ctx->stage_bytes = ctx->blocks[1].size & ~(ZVP_CRAW_PAGE_ALIGN - 1u);
        return 0;
    }
    if (ctx->total_slots >= 3u) {
        ctx->total_slots--;
        ctx->stage_pa = ctx->blocks[0].paddr + ctx->total_slots * ctx->frame_bytes;
        ctx->stage_bytes = ctx->frame_bytes & ~(ZVP_CRAW_PAGE_ALIGN - 1u);
        if (ctx->liro_installed && ctx->ops.phys_write) {
            ctx->shm_snap.slot_count = ctx->total_slots;
            (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, slot_count),
                                      &ctx->total_slots, sizeof(ctx->total_slots));
        }
        return 0;
    }
    return -2;
}

static void craw_build_raw_hdr_page(const zvp_craw_ctx_t *ctx, uint32_t frame_cnt,
                                    uint32_t drop_cnt, uint8_t out_page[ZVP_CRAW_FILE_HDR_BYTES]) {
    zvp_craw_file_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = ZVP_CRAW_FILE_HDR_MAGIC;
    hdr.version = 1u;
    hdr.width = ctx->width;
    hdr.height = ctx->height;
    hdr.bpp = (ctx->enc_type == 1u) ? 16u : ZVP_CRAW_DEF_BPP;
    hdr.enc_type = ctx->enc_type;
    hdr.fps_num = 25000u;
    hdr.fps_den = 1000u;
    hdr.frame_bytes = ctx->frame_bytes;
    hdr.frame_count = frame_cnt;
    hdr.drop_count = drop_cnt;
    zvp_craw_build_raw_header(out_page, ZVP_CRAW_FILE_HDR_BYTES, &hdr);
}

static int craw_write_stage_hdr_page(zvp_craw_ctx_t *ctx, int fh, const uint8_t page[ZVP_CRAW_FILE_HDR_BYTES]) {
    if (fh < 0 || ctx->ops.phys_write(ctx->stage_pa, page, ZVP_CRAW_FILE_HDR_BYTES) != 0) {
        return -1;
    }
    zvp_craw_extent_t ext = { ctx->stage_pa, ZVP_CRAW_FILE_HDR_BYTES };
    int wr = ctx->ops.fsys_write_direct(fh, ZVP_CRAW_PAGE_ALIGN, &ext, 1u, ZVP_CRAW_FILE_HDR_BYTES);
    return (wr == (int)ZVP_CRAW_FILE_HDR_BYTES) ? 0 : -2;
}

static int craw_flush_wav_pages(zvp_craw_ctx_t *ctx, bool pad_tail) {
    if (ctx->wav_fh < 0) return 0;

    if (!ctx->wav_first_flushed) {
        if (!pad_tail && ctx->wav_stage_used < ZVP_CRAW_FILE_HDR_BYTES) {
            return 0;
        }
        if (ctx->wav_stage_used < ZVP_CRAW_FILE_HDR_BYTES) {
            memset(ctx->wav_first_page + ctx->wav_stage_used, 0,
                   ZVP_CRAW_FILE_HDR_BYTES - ctx->wav_stage_used);
        }
        if (craw_write_stage_hdr_page(ctx, ctx->wav_fh, ctx->wav_first_page) != 0) {
            return -1;
        }
        ctx->wav_first_flushed = true;
        ctx->wav_stage_used = 0u;
        return 0;
    }

    uint32_t wav_buf_pa = ctx->stage_pa + ZVP_CRAW_FILE_HDR_BYTES;
    uint32_t aligned = pad_tail
        ? ((ctx->wav_stage_used + ZVP_CRAW_PAGE_ALIGN - 1u) & ~(ZVP_CRAW_PAGE_ALIGN - 1u))
        : (ctx->wav_stage_used & ~(ZVP_CRAW_PAGE_ALIGN - 1u));
    if (aligned == 0u) return 0;

    if (pad_tail && aligned > ctx->wav_stage_used) {
        memset(ctx->scratch_page, 0, aligned - ctx->wav_stage_used);
        (void)ctx->ops.phys_write(wav_buf_pa + ctx->wav_stage_used, ctx->scratch_page, aligned - ctx->wav_stage_used);
    }
    zvp_craw_extent_t ext = { wav_buf_pa, aligned };
    int wr = ctx->ops.fsys_write_direct(ctx->wav_fh, ZVP_CRAW_PAGE_ALIGN, &ext, 1u, aligned);
    if (wr != (int)aligned) {
        return -2;
    }

    uint32_t rem = (ctx->wav_stage_used > aligned) ? (ctx->wav_stage_used - aligned) : 0u;
    if (rem > 0u) {
        if (ctx->ops.phys_read(wav_buf_pa + aligned, ctx->scratch_page, rem) == 0) {
            (void)ctx->ops.phys_write(wav_buf_pa, ctx->scratch_page, rem);
        }
    }
    ctx->wav_stage_used = rem;
    return 0;
}

static int craw_append_wav_phys_chunk(zvp_craw_ctx_t *ctx, uint32_t src_pa, uint32_t len) {
    uint32_t wav_buf_pa = ctx->stage_pa + ZVP_CRAW_FILE_HDR_BYTES;
    uint32_t wav_buf_cap = ctx->stage_bytes - ZVP_CRAW_FILE_HDR_BYTES;

    while (len > 0u) {
        if (!ctx->wav_first_flushed) {
            uint32_t room = ZVP_CRAW_FILE_HDR_BYTES - ctx->wav_stage_used;
            uint32_t take = (len < room) ? len : room;
            if (take > 0u) {
                if (ctx->ops.phys_read(src_pa, ctx->wav_first_page + ctx->wav_stage_used, take) != 0) {
                    return -1;
                }
                ctx->wav_stage_used += take;
                ctx->clip_audio_bytes += take;
                src_pa += take;
                len -= take;
            }
            if (ctx->wav_stage_used == ZVP_CRAW_FILE_HDR_BYTES) {
                if (craw_flush_wav_pages(ctx, false) != 0) return -2;
            }
            continue;
        }

        if (ctx->wav_stage_used >= wav_buf_cap) {
            if (craw_flush_wav_pages(ctx, false) != 0) return -3;
        }
        uint32_t room = wav_buf_cap - ctx->wav_stage_used;
        uint32_t take = (len < room) ? len : room;
        if (take > sizeof(ctx->scratch_page)) take = sizeof(ctx->scratch_page);
        if (ctx->ops.phys_read(src_pa, ctx->scratch_page, take) != 0 ||
            ctx->ops.phys_write(wav_buf_pa + ctx->wav_stage_used, ctx->scratch_page, take) != 0) {
            return -4;
        }
        ctx->wav_stage_used += take;
        ctx->clip_audio_bytes += take;
        src_pa += take;
        len -= take;
    }
    if (ctx->wav_first_flushed && ctx->wav_stage_used >= ZVP_CRAW_PAGE_ALIGN) {
        return craw_flush_wav_pages(ctx, false);
    }
    return 0;
}

static void craw_drain_audio_ring(zvp_craw_ctx_t *ctx, const zvp_craw_shm_t *shm) {
    if (ctx->wav_fh < 0) return;

    uint32_t adf[4] = { 0u, 0u, 0u, 0u };
    if (ctx->ops.phys_read(0x041FDD34u + 0x3Cu, adf, sizeof(adf)) != 0) return;
    uint32_t base = adf[0];
    uint32_t sz   = adf[1];
    uint32_t wp   = adf[2];
    if (base == 0u || sz < 6144u || sz > (8u * 1048576u) || wp < base || wp >= (base + sz)) {
        return;
    }

    if (ctx->audio_ring_base != base || ctx->audio_ring_size != sz) {
        ctx->audio_ring_base = base;
        ctx->audio_ring_size = sz;
        if (shm->audio_ptr >= base && shm->audio_ptr < (base + sz)) {
            ctx->audio_last_wp = shm->audio_ptr;
        } else {
            ctx->audio_last_wp = wp;
        }
    }
    if (wp == ctx->audio_last_wp) return;

    if (wp > ctx->audio_last_wp) {
        (void)craw_append_wav_phys_chunk(ctx, ctx->audio_last_wp, wp - ctx->audio_last_wp);
    } else {
        (void)craw_append_wav_phys_chunk(ctx, ctx->audio_last_wp, (base + sz) - ctx->audio_last_wp);
        if (wp > base) {
            (void)craw_append_wav_phys_chunk(ctx, base, wp - base);
        }
    }
    ctx->audio_last_wp = wp;
}

static int craw_finalize_clip(zvp_craw_ctx_t *ctx, uint32_t drop_seq) {
    if (ctx->wav_fh >= 0) {
        zvp_craw_build_wav_header(ctx->wav_first_page, 48000u, 2u, 16u, ctx->clip_audio_bytes);
        if (!ctx->wav_first_flushed) {
            (void)craw_flush_wav_pages(ctx, true);
        } else {
            (void)craw_flush_wav_pages(ctx, true);
            if (ctx->ops.fsys_seek && ctx->ops.fsys_seek(ctx->wav_fh, 0ull, 0) == 0) {
                (void)craw_write_stage_hdr_page(ctx, ctx->wav_fh, ctx->wav_first_page);
            }
        }
        (void)ctx->ops.fsys_close(ctx->wav_fh);
        ctx->wav_fh = -1;
    }

    if (ctx->raw_fh >= 0) {
        if (ctx->ops.fsys_seek && ctx->ops.fsys_seek(ctx->raw_fh, 0ull, 0) == 0) {
            craw_build_raw_hdr_page(ctx, ctx->clip_frames, drop_seq, ctx->scratch_page);
            (void)craw_write_stage_hdr_page(ctx, ctx->raw_fh, ctx->scratch_page);
        }
        (void)ctx->ops.fsys_close(ctx->raw_fh);
        ctx->raw_fh = -1;
    }

    ctx->rec_clip_open = false;
    ctx->clip_drops = drop_seq;
    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             "已录 ZVC%05u (%u帧/%u丢/%uKB音)",
             ctx->clip_seq % 100000u, ctx->clip_frames, drop_seq,
             ctx->clip_audio_bytes / 1024u);
    craw_logf(ctx, "[craw] rec clip finalized ZVC%05u: frames=%u drops=%u wav_bytes=%u",
              ctx->clip_seq % 100000u, (unsigned)ctx->clip_frames,
              (unsigned)drop_seq, (unsigned)ctx->clip_audio_bytes);
    craw_log_v18_summary(ctx, "rec");
    ctx->clip_seq++;
    if (ctx->ops.fsys_get_free_mb) {
        (void)ctx->ops.fsys_get_free_mb(&ctx->free_sd_mb);
    }
    return 0;
}

static void craw_log_v18_summary(zvp_craw_ctx_t *ctx, const char *tag) {
    if (!ctx || !ctx->ops.phys_read) return;
    zvp_craw_dbg_t *d = (zvp_craw_dbg_t *)calloc(1u, sizeof(zvp_craw_dbg_t));
    if (!d) return;
    if (ctx->ops.phys_read(ZVP_CRAW_DBG_PA, d, sizeof(*d)) == 0 &&
        d->magic == ZVP_CRAW_DBG_MAGIC) {
        craw_logf(ctx,
                  "[craw_v18:%s] mask=0x%x aif_lr=[R1:%08x W0:%08x W1:%08x W2:%08x] "
                  "rec_lr=[R1:%08x W0:%08x W1:%08x W2:%08x] "
                  "rdcw1_s7=(%u,lr=%08x,cfg=%08x/%08x/%08x/%08x) "
                  "rdcw1_s8=(%u,lr=%08x,r1=%x)",
                  tag, (unsigned)d->v18_ext_installed_mask,
                  (unsigned)d->v18_aif_en_lr[1], (unsigned)d->v18_aif_en_lr[3],
                  (unsigned)d->v18_aif_en_lr[4], (unsigned)d->v18_aif_en_lr[5],
                  (unsigned)d->v18_aif_en_rec_lr[1], (unsigned)d->v18_aif_en_rec_lr[3],
                  (unsigned)d->v18_aif_en_rec_lr[4], (unsigned)d->v18_aif_en_rec_lr[5],
                  (unsigned)d->v18_rdcw1_s7_calls, (unsigned)d->v18_rdcw1_s7_lr,
                  (unsigned)d->v18_rdcw1_s7_cfg[0], (unsigned)d->v18_rdcw1_s7_cfg[1],
                  (unsigned)d->v18_rdcw1_s7_cfg[2], (unsigned)d->v18_rdcw1_s7_cfg[3],
                  (unsigned)d->v18_rdcw1_s8_calls, (unsigned)d->v18_rdcw1_s8_lr,
                  (unsigned)d->v18_rdcw1_s8_r1);
        craw_logf(ctx,
                  "[craw_v18:%s] s2=(%u,v=%08x,lr=%08x,%x,%x,%x) "
                  "s17=(%u,v=%08x,lr=%08x,w=%08x/%08x/%08x/%08x) "
                  "s22=(%u,lr=%08x,%x,%x,%x) s23=(%u,lr=%08x,%x) "
                  "ctrl=[%08x,%08x,%08x,%08x,%08x,%08x] "
                  "dev=[c04=%08x,c30=%08x,c68=%08x,ce0=%08x,d00=%08x,500=%08x]",
                  tag, (unsigned)d->v18_ctrl_s2_calls, (unsigned)d->v18_ctrl_s2_vptr,
                  (unsigned)d->v18_ctrl_s2_lr, (unsigned)d->v18_ctrl_s2_r1,
                  (unsigned)d->v18_ctrl_s2_r2, (unsigned)d->v18_ctrl_s2_r3,
                  (unsigned)d->v18_ctrl_s17_calls, (unsigned)d->v18_ctrl_s17_vptr,
                  (unsigned)d->v18_ctrl_s17_lr, (unsigned)d->v18_ctrl_s17_w[0],
                  (unsigned)d->v18_ctrl_s17_w[1], (unsigned)d->v18_ctrl_s17_w[2],
                  (unsigned)d->v18_ctrl_s17_w[3], (unsigned)d->v18_ctrl_s22_calls,
                  (unsigned)d->v18_ctrl_s22_lr, (unsigned)d->v18_ctrl_s22_r1,
                  (unsigned)d->v18_ctrl_s22_r2, (unsigned)d->v18_ctrl_s22_r3,
                  (unsigned)d->v18_ctrl_s23_calls, (unsigned)d->v18_ctrl_s23_lr,
                  (unsigned)d->v18_ctrl_s23_r1,
                  (unsigned)d->v18_rec_ctrl_dump[0], (unsigned)d->v18_rec_ctrl_dump[1],
                  (unsigned)d->v18_rec_ctrl_dump[2], (unsigned)d->v18_rec_ctrl_dump[3],
                  (unsigned)d->v18_rec_ctrl_dump[4], (unsigned)d->v18_rec_ctrl_dump[5],
                  (unsigned)d->v18_rec_camdev_regs[13], (unsigned)d->v18_rec_camdev_regs[15],
                  (unsigned)d->v18_rec_camdev_regs[16], (unsigned)d->v18_rec_camdev_regs[17],
                  (unsigned)d->v18_rec_camdev_regs[18], (unsigned)d->v18_rec_camdev_regs[22]);
        craw_logf(ctx,
                  "[craw_w0:%s] stg=%u(%u) arm=%u done=%u err=%u rc=(%d,%d %ux%u) "
                  "head=(408=%08x,40c=%08x,438=%08x,43c=%08x,440=%08x,444=%08x,448=%08x,44c=%08x,458=%08x,478=%08x) "
                  "iib=(4114=%08x,7064=%08x) rdc=(0c=%08x,b118=%08x,b178=%08x,b704=%x,b010=%x) w0_tick=(%08x,%08x,cu=%08x,sa=%08x,str=%x,ln=%x)",
                  tag, (unsigned)d->dfe_ch_pa[5], (unsigned)d->dfe_ch_pa[6],
                  (unsigned)d->rec_w0_arm_cnt, (unsigned)d->rec_w0_done_cnt,
                  (unsigned)d->rec_w0_err_cnt, (int)d->w0_raw_cfg_rc[0], (int)d->w0_raw_cfg_rc[1],
                  (unsigned)d->w0_raw_cfg_rc[2], (unsigned)d->w0_raw_cfg_rc[3],
                  (unsigned)d->w0_head_regs[2], (unsigned)d->w0_head_regs[3],
                  (unsigned)d->w0_head_regs[14], (unsigned)d->w0_head_regs[15],
                  (unsigned)d->w0_head_regs[16], (unsigned)d->w0_head_regs[17],
                  (unsigned)d->w0_head_regs[18], (unsigned)d->w0_head_regs[19],
                  (unsigned)d->w0_head_regs[22], (unsigned)d->w0_head_regs[23],
                  (unsigned)d->aif_bus_rec_arm[0], (unsigned)d->aif_bus_rec_arm[4],
                  (unsigned)d->rdcw0_hw_rec_arm[2],
                  (unsigned)d->w0_raw_backup[7], (unsigned)d->w0_raw_backup[8],
                  (unsigned)d->rec_rdc_at_arm[3], (unsigned)d->rec_rdc_at_arm[4],
                  (unsigned)d->rec_w0_at_tick[0], (unsigned)d->rec_w0_at_tick[1],
                  (unsigned)d->rec_w0_at_tick[4], (unsigned)d->rec_w0_at_tick[8],
                  (unsigned)d->rec_w0_at_tick[9], (unsigned)d->rec_w0_at_tick[11]);
    }
    free(d);
}

static void craw_dbg_trace(zvp_craw_ctx_t *ctx, const zvp_craw_shm_t *shm,
                           uint32_t stage, uint32_t v1, uint32_t v2, uint32_t v3) {
    if (!ctx || !shm || shm->frame_stride < 0x100000u || !ctx->ops.phys_write) {
        return;
    }
    uint32_t words[4] = { stage, v1, v2, v3 };
    (void)ctx->ops.phys_write(ZVP_CRAW_DBG_PA + offsetof(zvp_craw_dbg_t, dfe_a_ch_cu) + 16u,
                              words, sizeof(words));
}

static void craw_log_w0_diag(zvp_craw_ctx_t *ctx, const zvp_craw_shm_t *shm, const char *tag) {
    if (!ctx || !shm || !ctx->ops.phys_read || !ctx->ops.log_msg) return;
    zvp_craw_dbg_t *d = (zvp_craw_dbg_t *)calloc(1u, sizeof(zvp_craw_dbg_t));
    if (!d) return;
    if (ctx->ops.phys_read(ZVP_CRAW_DBG_PA, d, sizeof(*d)) == 0 &&
        d->magic == ZVP_CRAW_DBG_MAGIC) {
        craw_logf(ctx,
                  "[w0_diag:%s] stg=%u(%u) rec=%u pre=%u post=%u arm=%u done=%u err=%u wseq=%u fn=%d rect=%d (%ux%u) en=%u",
                  tag, (unsigned)d->dfe_ch_pa[5], (unsigned)d->dfe_ch_pa[6],
                  (unsigned)shm->rec_state, (unsigned)d->state_enc_pre_cnt,
                  (unsigned)d->state_enc_post_cnt, (unsigned)d->rec_w0_arm_cnt,
                  (unsigned)d->rec_w0_done_cnt, (unsigned)d->rec_w0_err_cnt,
                  (unsigned)shm->write_seq, (int)d->w0_raw_cfg_rc[0], (int)d->w0_raw_cfg_rc[1],
                  (unsigned)d->w0_raw_cfg_rc[2], (unsigned)d->w0_raw_cfg_rc[3],
                  (unsigned)d->w0_raw_backup[11]);
        craw_logf(ctx,
                  "[w0_hw:%s] top=%08x c118=%08x c178=%08x h408=%08x h40c=%08x h478=%08x rdc=%08x "
                  "ctrl=%08x st=%08x cu=%08x lst=(%08x,%08x) w14=%08x sa=%08x str=%x ln=%x iib=(%08x,%08x)",
                  tag, (unsigned)d->cam_ctrl_rec[0], (unsigned)d->cam_ctrl_rec[2],
                  (unsigned)d->cam_ctrl_rec[5], (unsigned)d->w0_head_regs[2],
                  (unsigned)d->w0_head_regs[3], (unsigned)d->w0_head_regs[23],
                  (unsigned)d->cam_ctrl_rec[20],
                  (unsigned)d->rec_w0_at_tick[0], (unsigned)d->rec_w0_at_tick[1],
                  (unsigned)d->rec_w0_at_tick[4], (unsigned)d->rec_w0_last_stat,
                  (unsigned)d->rec_w0_last_cu, (unsigned)d->rec_w0_at_tick[5],
                  (unsigned)d->rec_w0_at_tick[8], (unsigned)d->rec_w0_at_tick[9],
                  (unsigned)d->rec_w0_at_tick[11], (unsigned)d->aif_bus_rec_arm[0],
                  (unsigned)d->aif_bus_rec_arm[4]);
        craw_logf(ctx,
                  "[w0_hw2:%s] dcv=(e00=%08x,e10=%08x,e30=%08x,e34=%08x,e4c=%08x,f00=%08x,f10=%08x,f40=%08x) "
                  "13k=(%08x,%08x) top1=(148=%x,14c=%x,1a8=%x,1d8=%x)",
                  tag, (unsigned)d->dcv_hw_rec_arm[0], (unsigned)d->dcv_hw_rec_arm[1],
                  (unsigned)d->dcv_hw_rec_arm[2], (unsigned)d->dcv_hw_rec_arm[3],
                  (unsigned)d->dcv_hw_rec_arm[4], (unsigned)d->dcv_hw_rec_arm[5],
                  (unsigned)d->dcv_hw_rec_arm[6], (unsigned)d->dcv_hw_rec_arm[7],
                  (unsigned)d->dcv_hw_rec_arm[8], (unsigned)d->dcv_hw_rec_arm[9],
                  (unsigned)d->dcv_hw_rec_arm[10], (unsigned)d->dcv_hw_rec_arm[11],
                  (unsigned)d->dcv_hw_rec_arm[12], (unsigned)d->dcv_hw_rec_arm[13]);
        craw_logf(ctx,
                  "[w0_hw3:%s] rdcw0=(04=%08x,0c=%08x,18=%08x,400=%x,404=%x,500=%x,504=%x,600=%x) "
                  "freg=(08=%08x,20=%08x,28=%08x,2c=%08x,30=%08x,3c=%08x,e2c=%08x,e3c=%08x,904=%08x,980=%08x)",
                  tag, (unsigned)d->rdcw0_hw_rec_arm[1], (unsigned)d->rdcw0_hw_rec_arm[2],
                  (unsigned)d->rdc_10018,
                  (unsigned)d->rdcw0_hw_rec_arm[10], (unsigned)d->rdcw0_hw_rec_arm[11],
                  (unsigned)d->rdcw0_hw_rec_arm[12], (unsigned)d->rdcw0_hw_rec_arm[13],
                  (unsigned)d->rdcw0_hw_rec_arm[14],
                  (unsigned)d->dcv_hw_rec_arm[14], (unsigned)d->dcv_hw_rec_arm[15],
                  (unsigned)d->dcv_hw_rec_fregs[0], (unsigned)d->dcv_hw_rec_fregs[1],
                  (unsigned)d->dcv_hw_rec_fregs[2], (unsigned)d->dcv_hw_rec_fregs[5],
                  (unsigned)d->dcv_e2c, (unsigned)d->dcv_e3c,
                  (unsigned)d->dcv_hw_rec_fregs[6], (unsigned)d->dcv_hw_rec_fregs[7]);
    }
    free(d);
}

int zvp_craw_drainer_step(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->initialized || !ctx->ops.phys_read || !ctx->ops.phys_write ||
        !ctx->ops.fsys_open_direct || !ctx->ops.fsys_write_direct || !ctx->ops.fsys_close) {
        return -1;
    }

    zvp_craw_shm_t shm;
    memset(&shm, 0, sizeof(shm));
    if (ctx->ops.phys_read(ZVP_CRAW_SHM_PA, &shm, sizeof(shm)) != 0 ||
        shm.magic != ZVP_CRAW_SHM_MAGIC) {
        return 0;
    }
    ctx->shm_snap = shm;

    if (ctx->mode == ZVP_CRAW_LIRO_RAW_W0) {
        if (shm.rec_state != 0u) {
            ctx->diag_rec_ticks++;
            if (ctx->diag_last_rec_state == 0u) {
                craw_log_w0_diag(ctx, &shm, "start");
            } else if ((ctx->diag_rec_ticks <= 450u && (ctx->diag_rec_ticks % 15u) == 0u) ||
                       (ctx->diag_rec_ticks % 125u) == 15u) {
                craw_log_w0_diag(ctx, &shm, "tick");
            }
        } else if (ctx->diag_last_rec_state != 0u) {
            craw_log_w0_diag(ctx, &shm, "stop");
            ctx->diag_rec_ticks = 0u;
        }
    }
    ctx->diag_last_rec_state = shm.rec_state;

    if (!ctx->rec_clip_open) {
        if ((ctx->mode != ZVP_CRAW_LIRO_ARM_2K && ctx->mode != ZVP_CRAW_LIRO_RAW_W0) ||
            shm.rec_state == 0u ||
            shm.ring_base_pa == 0u || shm.slot_count <= 1u || shm.frame_stride == 0u ||
            shm.write_seq == shm.read_seq) {
            return 0;
        }
        if ((shm.frame_stride % ZVP_CRAW_PAGE_ALIGN) == 0u) {
            ctx->frame_bytes = shm.frame_stride;
            ctx->total_slots = shm.slot_count;
        }
        if (shm.target_w != 0u) ctx->width = shm.target_w;
        if (shm.target_h != 0u) ctx->height = shm.target_h;
        if (shm.target_enc_type != 0u) ctx->enc_type = shm.target_enc_type;

        if (craw_ensure_stage_buffer(ctx) != 0) {
            set_status(ctx, -10, "DMM 暂存区不可用");
            craw_logf(ctx, "[craw] drainer stage_buf unavail");
            return -10;
        }
        char raw_path[32], wav_path[32];
        zvp_craw_format_paths(ctx->clip_seq, raw_path, sizeof(raw_path), wav_path, sizeof(wav_path));

        int rfh = -1;
        int orc = ctx->ops.fsys_open_direct(raw_path, &rfh);
        if (orc != 0 || rfh < 0) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg), "开RAW失败 0x%08x", (unsigned)orc);
            craw_logf(ctx, "[craw] open %s failed rc=0x%08x fh=%d", raw_path, (unsigned)orc, rfh);
            ctx->last_err = orc ? orc : -11;
            return ctx->last_err;
        }

        craw_build_raw_hdr_page(ctx, 0u, 0u, ctx->scratch_page);
        if (craw_write_stage_hdr_page(ctx, rfh, ctx->scratch_page) != 0) {
            (void)ctx->ops.fsys_close(rfh);
            set_status(ctx, -12, "写RAW占位头失败");
            craw_logf(ctx, "[craw] write hdr %s failed", raw_path);
            return -12;
        }

        int wfh = -1;
        if (ctx->ops.fsys_open_direct(wav_path, &wfh) != 0 || wfh < 0) {
            wfh = -1;
        }
        craw_logf(ctx, "[craw] rec clip open %s (w=%u h=%u stride=0x%x slots=%u wfh=%d)",
                  raw_path, (unsigned)ctx->width, (unsigned)ctx->height,
                  (unsigned)ctx->frame_bytes, (unsigned)ctx->total_slots, wfh);

        ctx->raw_fh = rfh;
        ctx->wav_fh = wfh;
        ctx->rec_clip_open = true;
        ctx->clip_frames = 0u;
        ctx->clip_drops = 0u;
        ctx->clip_audio_bytes = 0u;
        ctx->audio_ring_base = 0u;
        ctx->audio_ring_size = 0u;
        ctx->audio_last_wp = 0u;
        ctx->wav_first_flushed = false;
        ctx->wav_stage_used = 44u;
        memset(ctx->wav_first_page, 0, sizeof(ctx->wav_first_page));
        zvp_craw_build_wav_header(ctx->wav_first_page, 48000u, 2u, 16u, 0u);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg), "正在录制 %s...", raw_path);
    }

    /* 1. 排空环形物理池中的已写入帧 (支持最多 2 段回绕合并直写) */
    uint32_t rseq = shm.read_seq;
    uint32_t wseq = shm.write_seq;
    uint32_t avail = wseq - rseq;
    if (avail > 0u && shm.slot_count > 0u && shm.frame_stride != 0u && shm.ring_base_pa != 0u) {
        if (avail > shm.slot_count) {
            avail = shm.slot_count;
            rseq = wseq - avail;
        }
        /* 单次直写按 ~9 MiB 分块（兼容 4KB 测试跨尾同时避免大块超限/堵死 I/O） */
        uint32_t max_frames = (9u * 1048576u) / shm.frame_stride;
        if (max_frames < 1u) max_frames = 1u;
        if (avail > max_frames) {
            avail = max_frames;
        }
        uint32_t head_slot = rseq % shm.slot_count;
        zvp_craw_extent_t exts[2];
        uint32_t ext_cnt = 0u;
        if (head_slot + avail <= shm.slot_count) {
            exts[0].paddr = shm.ring_base_pa + head_slot * shm.frame_stride;
            exts[0].size  = avail * shm.frame_stride;
            ext_cnt = 1u;
        } else {
            uint32_t first_n = shm.slot_count - head_slot;
            exts[0].paddr = shm.ring_base_pa + head_slot * shm.frame_stride;
            exts[0].size  = first_n * shm.frame_stride;
            exts[1].paddr = shm.ring_base_pa;
            exts[1].size  = (avail - first_n) * shm.frame_stride;
            ext_cnt = 2u;
        }
        uint32_t total_bytes = avail * shm.frame_stride;
        craw_dbg_trace(ctx, &shm, 0x20u, (avail << 16) | ext_cnt, exts[0].paddr, total_bytes);
        int wr = ctx->ops.fsys_write_direct(ctx->raw_fh, ZVP_CRAW_PAGE_ALIGN, exts, ext_cnt, total_bytes);
        if (wr == (int)total_bytes) {
            uint32_t new_rseq = rseq + avail;
            (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, read_seq),
                                      &new_rseq, sizeof(new_rseq));
            ctx->shm_snap.read_seq = new_rseq;
            ctx->clip_frames += avail;
            ctx->frames_written += avail;
            ctx->last_err = 0;
            craw_dbg_trace(ctx, &shm, 0x21u, avail, (uint32_t)wr, new_rseq);
        } else {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg), "RAW写卡错 0x%08x", (unsigned)wr);
            craw_logf(ctx, "[craw] write raw err wr=%d total=%u", wr, (unsigned)total_bytes);
            craw_dbg_trace(ctx, &shm, 0xEEu, avail, (uint32_t)wr, total_bytes);
            ctx->last_err = wr ? wr : -13;
            uint32_t stop_rec = 0u;
            (void)ctx->ops.phys_write(ZVP_CRAW_SHM_PA + offsetof(zvp_craw_shm_t, rec_state),
                                      &stop_rec, sizeof(stop_rec));
            return craw_finalize_clip(ctx, shm.drop_seq);
        }
    }

    /* 2. 增量追赶 48kHz LPCM 音频环并满页直写 */
    craw_dbg_trace(ctx, &shm, 0x30u, ctx->wav_stage_used, ctx->clip_audio_bytes, ctx->audio_last_wp);
    craw_drain_audio_ring(ctx, &shm);
    craw_dbg_trace(ctx, &shm, 0x31u, ctx->wav_stage_used, ctx->clip_audio_bytes, ctx->audio_last_wp);
    ctx->clip_drops = shm.drop_seq;

    /* 3. 录像停止或强制退出且视频环已排空 -> 定稿头部并关闭文件 */
    if ((shm.rec_state == 0u || ctx->drainer_stop) && ctx->shm_snap.read_seq == shm.write_seq) {
        craw_dbg_trace(ctx, &shm, 0x40u, ctx->clip_frames, ctx->clip_audio_bytes, shm.drop_seq);
        int frc = craw_finalize_clip(ctx, shm.drop_seq);
        craw_dbg_trace(ctx, &shm, 0x41u, (uint32_t)frc, ctx->clip_seq, 0u);
        return frc;
    }
    return 0;
}

#if !defined(ZVP_HOST_TEST)
typedef int (*zvp_fn_pthread_create_t)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
typedef int (*zvp_fn_pthread_join_t)(pthread_t, void **);

static void *craw_drainer_thread_fn(void *arg) {
    zvp_craw_ctx_t *ctx = (zvp_craw_ctx_t *)arg;
    craw_logf(ctx, "[craw] drainer thread entered");
    while (!ctx->drainer_stop) {
        (void)zvp_craw_drainer_step(ctx);
        usleep(8000u);
    }
    (void)zvp_craw_drainer_step(ctx);
    craw_logf(ctx, "[craw] drainer thread exiting");
    return NULL;
}

static int start_drainer_thread(zvp_craw_ctx_t *ctx) {
    if (ctx->drainer_running) return 0;
    zvp_fn_pthread_create_t fn_create =
        (zvp_fn_pthread_create_t)dlsym(RTLD_DEFAULT, "pthread_create");
    if (!fn_create) {
        void *h = dlopen("libpthread.so.0", RTLD_LAZY);
        if (h) fn_create = (zvp_fn_pthread_create_t)dlsym(h, "pthread_create");
    }
    if (!fn_create) return -1;

    ctx->drainer_stop = false;
    pthread_t tid = 0;
    if (fn_create(&tid, NULL, craw_drainer_thread_fn, ctx) != 0) {
        return -2;
    }
    ctx->drainer_tid = (uint64_t)(uintptr_t)tid;
    ctx->drainer_running = true;
    return 0;
}

static void stop_drainer_thread(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->drainer_running) return;
    ctx->drainer_stop = true;
    zvp_fn_pthread_join_t fn_join =
        (zvp_fn_pthread_join_t)dlsym(RTLD_DEFAULT, "pthread_join");
    if (!fn_join) {
        void *h = dlopen("libpthread.so.0", RTLD_LAZY);
        if (h) fn_join = (zvp_fn_pthread_join_t)dlsym(h, "pthread_join");
    }
    if (fn_join && ctx->drainer_tid != 0u) {
        (void)fn_join((pthread_t)(uintptr_t)ctx->drainer_tid, NULL);
    }
    ctx->drainer_tid = 0u;
    ctx->drainer_running = false;
    ctx->drainer_stop = false;
}
#else
static int start_drainer_thread(zvp_craw_ctx_t *ctx) {
    (void)ctx;
    return 0;
}

static void stop_drainer_thread(zvp_craw_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->rec_clip_open) {
        ctx->drainer_stop = true;
        (void)zvp_craw_drainer_step(ctx);
        ctx->drainer_stop = false;
    }
}
#endif

void zvp_craw_shutdown(zvp_craw_ctx_t *ctx) {
    if (!ctx || !ctx->initialized) return;
    stop_drainer_thread(ctx);
    (void)zvp_craw_uninstall_liro(ctx);
    zvp_craw_free_pool(ctx);
}
