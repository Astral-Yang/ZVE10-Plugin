#ifndef ZVP_CRAW_SHM_H
#define ZVP_CRAW_SHM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ZV-E10 av-cam.bin 物理内存映射与尾部空闲区坐标 (阶段 1 & 2 已实机核验)
 * ========================================================================== */
#define ZVP_AVCAM_VMA_BASE          0x635C6000u
#define ZVP_AVCAM_PA_BASE           0x035C6000u
#define ZVP_AVCAM_VMA_TO_PA(vma)    ((uint32_t)(vma) - 0x60000000u)

/* LIRO 尾部空闲区 (1.67 MiB): VMA 0x6471AC00..0x648C5FFC */
#define ZVP_CRAW_SHM_VMA            0x6471AC00u
#define ZVP_CRAW_SHM_PA             0x0471AC00u
#define ZVP_CRAW_SHM_REGION_SIZE    0x00000100u /* 256 B */

#define ZVP_CRAW_CODE_VMA           0x6471AD00u
#define ZVP_CRAW_CODE_PA            0x0471AD00u
#define ZVP_CRAW_CODE_MAX_SIZE      0x00004000u /* 16 KiB 预留 */

#define ZVP_CRAW_DBG_VMA            0x6471F000u
#define ZVP_CRAW_DBG_PA             0x0471F000u
#define ZVP_CRAW_DBG_SIZE           0x00001000u /* 4 KiB 扩展调试观测区 */
#define ZVP_CRAW_DBG_MAGIC          0x5A564442u /* "ZVDB" */

#define ZVP_CRAW_SHM_MAGIC          0x5A564352u /* "ZVCR" */
#define ZVP_CRAW_SHM_VERSION        1u

/* av-cam.bin .data 可写指针槽位 & 原厂函数地址 (实机 probe_liro_rt 100% MATCH) */
#define ZVP_AVCAM_SLOT_CAMRDC_CFG   0x64587264u
#define ZVP_AVCAM_ORIG_CAMRDC_CFG   0x63775281u

#define ZVP_AVCAM_SLOT_RDCW0_S7     0x645874A4u
#define ZVP_AVCAM_ORIG_RDCW0_S7     0x637760E1u

#define ZVP_AVCAM_SLOT_CAMAIF_S4    0x64588798u
#define ZVP_AVCAM_ORIG_CAMAIF_S4    0x6379E1F5u

#define ZVP_AVCAM_SLOT_IFMT_S4      0x645887C8u
#define ZVP_AVCAM_ORIG_IFMT_S4      0x63D23BA5u

#define ZVP_AVCAM_SLOT_IFMT_S10     0x645887E0u
#define ZVP_AVCAM_ORIG_IFMT_S10     0x63D23B37u

#define ZVP_AVCAM_SLOT_SEQ_MOVIE    0x6463A01Cu
#define ZVP_AVCAM_ORIG_SEQ_MOVIE    0x6393DF75u

#define ZVP_AVCAM_SLOT_G_AIF_MGR    0x6463A2FCu
#define ZVP_AVCAM_ORIG_G_AIF_MGR    0x645BACF8u

#define ZVP_AVCAM_SLOT_DFE_RDC_S10  0x64587280u /* CamCore::CamAif::Enable */
#define ZVP_AVCAM_ORIG_DFE_RDC_S10  0x63D0A877u

#define ZVP_AVCAM_SLOT_DFE_RDC_CFG  0x64587284u /* CamCore::CamAif::Disable */
#define ZVP_AVCAM_ORIG_DFE_RDC_CFG  0x63D0A8A1u

#define ZVP_AVCAM_SLOT_DFE_MAIN     0x64639128u /* Sequence_Movie.cpp #2 */
#define ZVP_AVCAM_ORIG_DFE_MAIN     0x63940965u

#define ZVP_AVCAM_SLOT_STATE_HDL    0x64575E28u /* StateCache/State msg handler (hsMovieRec @ +0x52) */
#define ZVP_AVCAM_ORIG_STATE_HDL    0x636ACEA5u

#define ZVP_AVCAM_SLOT_DFE_RDCW0    0x64588B58u /* DFECore::RdcW0::slot[10] */
#define ZVP_AVCAM_ORIG_DFE_RDCW0    0x637A32BDu

/* v18: CamCore 全链路由与 RdcW1 扩展只读 Spy 槽位 (均在 .data/vtable 页) */
#define ZVP_AVCAM_EXT_RDCW1_S7      0x645874D4u /* CamCore::RdcW1::slot[7] */
#define ZVP_AVCAM_EXT_ORIG_RDCW1_S7 0x6377600Du

#define ZVP_AVCAM_EXT_RDCW1_S8      0x645874D8u /* CamCore::RdcW1::slot[8] */
#define ZVP_AVCAM_EXT_ORIG_RDCW1_S8 0x63D0DC5Du

#define ZVP_AVCAM_EXT_SPL_EE_S2     0x645870A0u /* SplitEEController::slot[2] */
#define ZVP_AVCAM_EXT_ORIG_SPL_EE_S2 0x637730C5u

#define ZVP_AVCAM_EXT_EE_S2         0x64586F20u /* EEController::slot[2] */
#define ZVP_AVCAM_EXT_ORIG_EE_S2    0x637730C5u

#define ZVP_AVCAM_EXT_SPL_EE_S17    0x645870DCu /* SplitEEController::slot[17] (W0) */
#define ZVP_AVCAM_EXT_ORIG_SPL_EE_S17 0x637721EDu

#define ZVP_AVCAM_EXT_EE_S17        0x64586F5Cu /* EEController::slot[17] (W0) */
#define ZVP_AVCAM_EXT_ORIG_EE_S17   0x637721EDu

#define ZVP_AVCAM_EXT_SPL_EE_S22    0x645870F0u /* SplitEEController::slot[22] */
#define ZVP_AVCAM_EXT_ORIG_SPL_EE_S22 0x63774A69u

#define ZVP_AVCAM_EXT_SPL_CAP_S23   0x6458708Cu /* SplitCapController::slot[23] */
#define ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23 0x63773D59u

#define ZVP_AVCAM_DFE_ISR_SLOTS     0x64138388u /* 4 * 16B */

/* 运行模式与标志位 */
typedef enum zvp_craw_liro_mode {
    ZVP_CRAW_LIRO_BYPASS   = 0u,
    ZVP_CRAW_LIRO_SPY_ONLY = 1u,
    ZVP_CRAW_LIRO_ARM_2K   = 2u,
    ZVP_CRAW_LIRO_RAW_W0   = 3u  /* W0 真 Bayer RAW 源路由 (HEAD0 -> RdcW0 Bypass -> W0) */
} zvp_craw_liro_mode_t;

#define ZVP_CRAW_FLAG_OVERRIDE_RDC  (1u << 0)
#define ZVP_CRAW_FLAG_OVERRIDE_RING (1u << 1)
#define ZVP_CRAW_FLAG_COUNT_ISR     (1u << 2)

#define ZVP_CRAW_HOOK_BIT_SEQ       (1u << 0)
#define ZVP_CRAW_HOOK_BIT_RDCW0_S7  (1u << 1)
#define ZVP_CRAW_HOOK_BIT_CAMAIF_S4 (1u << 2)
#define ZVP_CRAW_HOOK_BIT_IFMT_S4   (1u << 3)
#define ZVP_CRAW_HOOK_BIT_IFMT_S10  (1u << 4)
#define ZVP_CRAW_HOOK_BIT_RDC_CFG   (1u << 5)
#define ZVP_CRAW_HOOK_BIT_DFE_RDC   (1u << 6)
#define ZVP_CRAW_HOOK_BIT_DFE_RDC2  (1u << 7)
#define ZVP_CRAW_HOOK_BIT_DFE_MAIN  (1u << 8)
#define ZVP_CRAW_HOOK_BIT_STATE     (1u << 9)
#define ZVP_CRAW_HOOK_BIT_DFE_RDCW0 (1u << 10)

/* ============================================================================
 * 共享控制块 (固定位于 PA 0x0471AC00 / VMA 0x6471AC00，大小 <= 256 B)
 * ========================================================================== */
typedef struct zvp_craw_shm {
    /* 0x00..0x1F: 标识与控制参数 */
    uint32_t magic;
    uint32_t version;
    volatile uint32_t mode;
    volatile uint32_t flags;
    volatile uint32_t rec_state;
    volatile uint32_t target_enc_type;
    volatile uint32_t target_w;
    volatile uint32_t target_h;

    /* 0x20..0x3F: DMM 环形物理缓冲描述符与生产者/消费者序号 */
    uint32_t ring_base_pa;
    uint32_t ring_total_bytes;
    uint32_t frame_stride;
    uint32_t slot_count;
    volatile uint32_t write_seq;
    volatile uint32_t read_seq;
    volatile uint32_t drop_seq;
    volatile uint32_t audio_ptr;

    /* 0x40..0x7F: Spy 模式捕获的原厂调用上下文快照 */
    volatile uint32_t seq_calls;
    volatile uint32_t seq_last_r0;
    volatile uint32_t seq_last_r1;
    volatile uint32_t seq_last_r2;

    volatile uint32_t rdc_calls;
    volatile uint32_t rdc_last_this;
    volatile uint32_t rdc_last_enc;
    volatile uint32_t rdc_last_w;
    volatile uint32_t rdc_last_h;

    volatile uint32_t aif_calls;
    volatile uint32_t aif_last_this;
    volatile uint32_t aif_last_r1;
    volatile uint32_t aif_last_port;
    volatile uint32_t ifmt_s4_calls;
    volatile uint32_t ifmt_s10_calls;
    volatile uint32_t isr_calls;

    /* 0x80..0x9F: CamRdc_cfg 观测与扩展 */
    volatile uint32_t rdc_cfg_calls;
    volatile uint32_t rdc_cfg_last_this;
    volatile uint32_t rdc_cfg_last_r1;
    volatile uint32_t rdc_cfg_last_r2;
    volatile uint32_t rdc_cfg_last_r3;
    volatile uint32_t last_ring_pa_written;
    volatile uint32_t state_calls;
    volatile uint32_t state_last_msg_hs; /* [15:0]=msg_id, [23:16]=hsMovieRec, [31:24]=byte53 */

    /* 0xA0..0xBF: 原厂指针备份与安装标记 */
    uint32_t orig_seq_movie;
    uint32_t orig_rdcw0_s7;
    uint32_t orig_camrdc_cfg;
    uint32_t orig_camaif_s4;
    uint32_t orig_ifmt_s4;
    uint32_t orig_ifmt_s10;
    uint32_t hook_installed_mask;
    uint32_t payload_bytes;

    /* 0xC0..0xFF: DFECore::RdcW0 与 DFE/State 主控扩展观测与原厂备份 */
    volatile uint32_t dfe_rdc_calls;
    volatile uint32_t dfe_rdc_enc;
    volatile uint32_t dfe_rdc_w;
    volatile uint32_t dfe_rdc_h;
    volatile uint32_t dfe_rdc2_calls;
    volatile uint32_t dfe_main_calls;
    volatile uint32_t dfe_main_r0;
    uint32_t orig_dfe_rdc_s10;
    uint32_t orig_dfe_rdc_cfg;
    uint32_t orig_dfe_main;
    volatile uint32_t rdc_last_lr;
    uint32_t orig_state_hdl;
    uint32_t orig_dfe_rdcw0;
    volatile uint32_t state_last_this;
    volatile uint32_t dfe_rdcw0_calls;
    volatile uint32_t dfe_rdcw0_last_cfg;
} zvp_craw_shm_t;

/* 头部跳转表位于 0x6471AD00（固定顺序，每个入口 4 字节为 Thumb 函数地址） */
#define ZVP_LIRO_TBL_SEQ_MOVIE   0u
#define ZVP_LIRO_TBL_RDCW0_S7    1u
#define ZVP_LIRO_TBL_CAMRDC_CFG  2u
#define ZVP_LIRO_TBL_CAMAIF_S4   3u
#define ZVP_LIRO_TBL_IFMT_S4     4u
#define ZVP_LIRO_TBL_IFMT_S10    5u
#define ZVP_LIRO_TBL_DFE_RDC_S10 6u
#define ZVP_LIRO_TBL_DFE_RDC_CFG 7u
#define ZVP_LIRO_TBL_DFE_MAIN    8u
#define ZVP_LIRO_TBL_STATE_HDL   9u
#define ZVP_LIRO_TBL_DFE_RDCW0   10u
#define ZVP_LIRO_TBL_COUNT       11u

#define ZVP_LIRO_EXT_RDCW1_S7    0u
#define ZVP_LIRO_EXT_RDCW1_S8    1u
#define ZVP_LIRO_EXT_SPL_EE_S2   2u
#define ZVP_LIRO_EXT_EE_S2       3u
#define ZVP_LIRO_EXT_SPL_EE_S17  4u
#define ZVP_LIRO_EXT_EE_S17      5u
#define ZVP_LIRO_EXT_SPL_EE_S22  6u
#define ZVP_LIRO_EXT_SPL_CAP_S23 7u
#define ZVP_LIRO_EXT_COUNT       8u

typedef struct zvp_liro_dispatch_header {
    uint32_t magic;
    uint32_t version;
    uint32_t entries[ZVP_LIRO_TBL_COUNT];
    uint32_t ext_entries[ZVP_LIRO_EXT_COUNT];
} zvp_liro_dispatch_header_t;

typedef struct zvp_craw_dbg {
    uint32_t magic;
    volatile uint32_t camaif0_en_calls;
    volatile uint32_t camaif0_dis_calls;
    volatile uint32_t camaif_port_hits[8]; /* Core0/1 CamAif Enable per port (0..7) */
    volatile uint32_t state_hs_nonzero_calls;
    volatile uint32_t state_hs_max;
    volatile uint32_t state_msg_idx;
    volatile uint32_t state_msg_hist[16];
    volatile uint32_t w0_at_rdc_valid;
    volatile uint32_t w0_at_rdc[32];      /* 0xF3002380..0xF30023FF before RdcW0_s7 */
    volatile uint32_t w0_after_rdc_valid;
    volatile uint32_t w0_after_rdc[32];   /* 0xF3002380..0xF30023FF after RdcW0_s7 */
    volatile uint32_t r0_at_rdc[32];      /* 0xF3002200..0xF300227F at RdcW0_s7 */
    volatile uint32_t w0_at_en_valid;
    volatile uint32_t w0_at_en[32];       /* 0xF3002380..0xF30023FF when CamAif_0 Enable */
    volatile uint32_t rdc_cfg_words[8];   /* cfg[0..7] passed to RdcW0_s7 */
    volatile uint32_t state_payload_idle[24];
    volatile uint32_t state_payload_rec[24];
    volatile uint32_t lpcm_wp_val;        /* *(0x6420b580) */

    /* 扩展录像/静照 DMA 与音频实测探测（保持总大小 <= 4096B） */
    volatile uint32_t state_enc_pre_cnt;  /* msg 0x0108 (ENC_PREPROC) count */
    volatile uint32_t state_enc_post_cnt; /* msg 0x0109 (ENC_POSTPROC) count */
    volatile uint32_t w1_at_rec_valid;
    volatile uint32_t w1_at_rec[16];      /* 0xF3002400..3F during ENC_PREPROC */
    volatile uint32_t w2_at_rec_valid;
    volatile uint32_t w2_at_rec[16];      /* 0xF3002480..BF during ENC_PREPROC */
    volatile uint32_t r0_at_rec[16];      /* 0xF3002200..3F during ENC_PREPROC */
    volatile uint32_t r1_at_rec[16];      /* 0xF3002280..BF during ENC_PREPROC */
    volatile uint32_t w1_at_idle[16];     /* 0xF3002400..3F during idle/still */
    volatile uint32_t w2_at_idle[16];     /* 0xF3002480..BF during idle/still */
    volatile uint32_t w0_at_dis[16];      /* 0xF3002380..BF at CamAif_0 Disable */
    volatile uint32_t dfe_port8_rec_valid;
    volatile uint32_t dfe_port8_sub_ch;   /* (base & 0xFFFF0000) | ch */
    volatile uint32_t dfe_port8_regs[20]; /* DFECore base+0x200+ch*0x80 (+0x00..+0x4C) */
    volatile uint32_t dfe_ch_pa[12];      /* 0xFC000200 + ch*0x80 + 0x28 during REC */
    volatile uint32_t dfe_ch_sz[12];      /* 0xFC000200 + ch*0x80 + 0x2C during REC */
    volatile uint32_t adf_buffmgr_rec[8]; /* *(0x642069AC + 0..7*4) during REC */
    volatile uint32_t adf_write_rec[8];   /* *(0x64206674 + 0..7*4) during REC */
    volatile uint32_t adf_buffmgr_nz_pa;  /* non-zero *(0x642069AC + 4) latch */
    volatile uint32_t adf_buffmgr_nz_sz;  /* non-zero *(0x642069AC + 16) latch */

    /* v7: 录像期间各模块精确活跃计数、AIF Core0/1 & DFECore 全通道快照、LPCM 全结构 */
    volatile uint32_t rec_aif_calls;
    volatile uint32_t rec_dfe_rdc_calls;
    volatile uint32_t rec_dfe_rdc2_calls;
    volatile uint32_t rec_ifmt_s4_calls;
    volatile uint32_t rec_dfe_main_calls;
    volatile uint32_t rec_last_camcore_this;
    volatile uint32_t rec_last_dfecore_this;
    volatile uint32_t rec_port_hits[16];     /* CamCore port 0..15 hits while rec_state != 0 */
    volatile uint32_t core0_rec_regs[6][5];  /* R0..W2 (+0x00, +0x10, +0x20, +0x24, +0x2C) in REC */
    volatile uint32_t core1_rec_regs[6][5];  /* Core1 R0..W2 (+0x00, +0x10, +0x20, +0x24, +0x2C) in REC */
    volatile uint32_t dfe_a_ch_cu[8];        /* 0xFC0A0200+c*0x80 +0x10 in REC */
    volatile uint32_t dfe_a_ch_pa[8];        /* 0xFC0A0200+c*0x80 +0x20 in REC */
    volatile uint32_t dfe_a_ch_sz[8];        /* 0xFC0A0200+c*0x80 +0x24 in REC */
    volatile uint32_t dfe_a_ch_lp[8];        /* 0xFC0A0200+c*0x80 +0x2C in REC */
    volatile uint32_t adf_buffmgr_full[36];  /* *(0x642069AC + 0..0x8C) during REC */

    /* v8: Phase-4A W0->DDR PA 实测验证、CamMux 控制寄存器与 0x641FDD34 音频实时追踪 */
    volatile uint32_t w0_orig_start_pa;      /* W0 +0x20 被重定向前的原值 (如 0x96000000) */
    volatile uint32_t w0_redirect_pa;        /* W0 +0x20 重定向后读回值 (期望 0x1A000000) */
    volatile uint32_t w0_redirect_cnt;       /* W0 地址重定向次数 */
    volatile uint32_t reserved_v8;
    volatile uint32_t cam_ctrl_still[24];    /* Still W0 Enable 时 0xF3000xxx/0xF3010xxx Mux 寄存器 */
    volatile uint32_t cam_ctrl_rec[24];      /* Movie REC 期间 0xF3000xxx/0xF3010xxx Mux 寄存器 */
    volatile uint32_t adf_1fdd34_rec[24];    /* *(0x641FDD34 + 0x30..+0x8C) during REC */
    volatile uint32_t adf_bm_tail_rec[16];   /* *(0x642069AC + 0x88..+0xBC), [14]=+0xE4, [15]=+0xE8 */
    volatile uint32_t adf_wp_min;            /* REC 期间 *(0x641FDD34+0x44) 最小值 */
    volatile uint32_t adf_wp_max;            /* REC 期间 *(0x641FDD34+0x44) 最大值 */
    volatile uint32_t adf_wp_changes;        /* REC 期间 *(0x641FDD34+0x44) 跳变次数 */
    volatile uint32_t adf_70_first;          /* REC 首次 *(0x641FDD34+0x70) */
    volatile uint32_t adf_70_last;           /* REC 末次 *(0x641FDD34+0x70) */

    /* v10: Movie REC W0 (RdcW0) 连续抓取诊断与状态机 */
    volatile uint32_t rec_w0_state;          /* 0=idle, 1=need_arm, 2=armed */
    volatile uint32_t rec_w0_arm_cnt;        /* 录像期间 W0 启动次数 */
    volatile uint32_t rec_w0_done_cnt;       /* 录像期间 W0 单帧完成次数 */
    volatile uint32_t rec_w0_err_cnt;        /* 录像期间异常/超时计数 */
    volatile uint32_t rec_w0_cur_pa;         /* 当前 W0 DMA 目标槽位 PA */
    volatile uint32_t rec_w0_last_cu;        /* 最近一次轮询到的 W0 +0x10 (cuAdrs) */
    volatile uint32_t rec_w0_last_stat;      /* 最近一次轮询到的 W0 +0x04 */
    volatile uint32_t rec_w0_last_ctrl;      /* 最近一次轮询到的 W0 +0x00 */
    volatile uint32_t rec_w0_at_arm[16];     /* Movie 首次 arm 后 W0 寄存器快照 */
    volatile uint32_t rec_w0_at_tick[16];    /* Movie REC 最新 W0 寄存器快照 */
    volatile uint32_t rec_rdc_at_arm[8];     /* Movie 首次 arm 后 RdcW0/Mux 快照 */

    /* v10d: 外部 AIF 总线表 (0xF2804114/7064/2034/0xF34021xx) 与 RdcW0 详细硬件快照 */
    volatile uint32_t aif_bus_still[16];     /* STILL hook_rdcw0_s7 时外部总线/W0窗口寄存器 */
    volatile uint32_t aif_bus_rec_arm[16];   /* REC 首次 arm 后外部总线/W0窗口寄存器 */
    volatile uint32_t rdcw0_hw_still[16];    /* STILL 0xF3010400..600 寄存器 */
    volatile uint32_t rdcw0_hw_rec_arm[16];  /* REC 首次 arm 后 0xF3010400..600 寄存器 */

    /* v18: 全链 RAW 源路由与 RdcW1/SplitEE 只读追踪观测区 */
    volatile uint32_t v18_ext_installed_mask;
    uint32_t          v18_ext_orig[ZVP_LIRO_EXT_COUNT];
    volatile uint32_t v18_aif_en_lr[8];      /* CamAif::Enable per port 最近一次 LR */
    volatile uint32_t v18_aif_en_rec_lr[8];  /* REC 期间 CamAif::Enable per port LR */
    volatile uint32_t v18_rdcw1_s7_calls;
    volatile uint32_t v18_rdcw1_s7_lr;
    volatile uint32_t v18_rdcw1_s7_cfg[8];
    volatile uint32_t v18_rdcw1_s8_calls;
    volatile uint32_t v18_rdcw1_s8_lr;
    volatile uint32_t v18_rdcw1_s8_r1;
    volatile uint32_t v18_ctrl_s2_calls;
    volatile uint32_t v18_ctrl_s2_vptr;
    volatile uint32_t v18_ctrl_s2_lr;
    volatile uint32_t v18_ctrl_s2_r1;
    volatile uint32_t v18_ctrl_s2_r2;
    volatile uint32_t v18_ctrl_s2_r3;
    volatile uint32_t v18_ctrl_s17_calls;
    volatile uint32_t v18_ctrl_s17_vptr;
    volatile uint32_t v18_ctrl_s17_lr;
    volatile uint32_t v18_ctrl_s17_r1;
    volatile uint32_t v18_ctrl_s17_w[8];     /* r1: [0],[1],[0x60],[0x68],[0x6c],[0x70],[0x74],[0x78] */
    volatile uint32_t v18_ctrl_s22_calls;
    volatile uint32_t v18_ctrl_s22_lr;
    volatile uint32_t v18_ctrl_s22_r1;
    volatile uint32_t v18_ctrl_s22_r2;
    volatile uint32_t v18_ctrl_s22_r3;
    volatile uint32_t v18_ctrl_s23_calls;
    volatile uint32_t v18_ctrl_s23_lr;
    volatile uint32_t v18_ctrl_s23_r1;
    volatile uint32_t v18_ctrl_s23_r2;
    volatile uint32_t v18_ctrl_s23_r3;
    volatile uint32_t v18_rec_ctrl_dump[16]; /* Controller +0x28..+0x64 (R0,W0,W1,R2,R1,W2) */
    volatile uint32_t v18_rec_camdev_regs[24]; /* REC 中 W1 Enable 时 0xF3000C00..E00 快照 */

    /* W0 真 Bayer RAW 源路由：HEAD0 寄存器快照、硬件现场备份与配置返回码 */
    volatile uint32_t w0_head_regs[24];      /* [0..21]=0xF3000400..454, [22]=0x458, [23]=0x478 */
    volatile uint32_t w0_raw_backup[12];     /* 0x408,40C,478,4114,7064,1000C + TOP0/RdcW0/pbl_en */
    volatile uint32_t w0_raw_cfg_rc[4];      /* [0]=set_func rc, [1]=set_rect rc, [2]=w, [3]=h */

    /* CamDcv & RdcW0 备份与实时硬件诊断快照 */
    volatile uint32_t dcv_raw_backup[16];    /* TOP0(0,8),148,14C,1A8,1D8,E00,E4C,F00,F40,13000,13080,Rdc(10004,10404,10504,10600) */
    volatile uint32_t dcv_hw_rec_arm[16];    /* E00,E10,E30,E34,E4C,F00,F10,F40,13000,13080,148,14C,1A8,1D8,F08,F20 */
    volatile uint32_t dcv_hw_rec_fregs[8];   /* F28,F2C,F30,F34,F38,F3C,904,980 */
    volatile uint32_t dcv_e2c;               /* 0xF3000E2C */
    volatile uint32_t dcv_e3c;               /* 0xF3000E3C */
    volatile uint32_t dcv_bak_e30;           /* 停录还原备份 0xF3000E30 */
    volatile uint32_t dcv_bak_e34;           /* 停录还原备份 0xF3000E34 */
    volatile uint32_t rdc_10018;             /* 0xF3010018 */
} zvp_craw_dbg_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(zvp_craw_shm_t) <= ZVP_CRAW_SHM_REGION_SIZE,
               "zvp_craw_shm_t exceeds 256-byte SHM region");
_Static_assert(sizeof(zvp_craw_dbg_t) <= ZVP_CRAW_DBG_SIZE,
               "zvp_craw_dbg_t exceeds 4KB DBG region");
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZVP_CRAW_SHM_H */
