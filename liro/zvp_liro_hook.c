#include <stdint.h>
#include "zvp_craw_shm.h"

#define SHM ((zvp_craw_shm_t *)ZVP_CRAW_SHM_VMA)
#define DBG ((zvp_craw_dbg_t *)ZVP_CRAW_DBG_VMA)

static inline void mem_barrier(void) {
    __asm__ volatile ("dmb" ::: "memory");
}

static void copy_mmio32(volatile uint32_t *dst, uint32_t mmio_va, uint32_t words) {
    const volatile uint32_t *src = (const volatile uint32_t *)mmio_va;
    for (uint32_t i = 0; i < words; ++i) {
        dst[i] = src[i];
    }
}

static void ensure_dbg_init(zvp_craw_dbg_t *d) {
    if (d->magic != ZVP_CRAW_DBG_MAGIC) {
        uint32_t *p = (uint32_t *)d;
        for (uint32_t i = 0; i < sizeof(*d) / 4u; ++i) {
            p[i] = 0u;
        }
        d->magic = ZVP_CRAW_DBG_MAGIC;
    }
}

/* 记录 Sequence_Movie 入口参数 */
void zvp_spy_seq_movie(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->seq_calls++;
    s->seq_last_r0 = r0;
    s->seq_last_r1 = r1;
    s->seq_last_r2 = r2;
    mem_barrier();
}

/* 记录 CamRdc::cfg 参数 */
void zvp_spy_camrdc_cfg(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->rdc_cfg_calls++;
    s->rdc_cfg_last_this = r0;
    s->rdc_cfg_last_r1 = r1;
    s->rdc_cfg_last_r2 = r2;
    s->rdc_cfg_last_r3 = r3;
    mem_barrier();
}

static void snap_aif_5(volatile uint32_t dst[5], uint32_t base_va) {
    dst[0] = *(const volatile uint32_t *)(base_va + 0x00u);
    dst[1] = *(const volatile uint32_t *)(base_va + 0x10u);
    dst[2] = *(const volatile uint32_t *)(base_va + 0x20u);
    dst[3] = *(const volatile uint32_t *)(base_va + 0x24u);
    dst[4] = *(const volatile uint32_t *)(base_va + 0x2Cu);
}

/* 记录 CamAif::slot[4] 参数与 Port ID (DFECore::CamAif::Enable @ 0x6379E1F5) */
void zvp_spy_camaif_s4(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    s->aif_calls++;
    s->aif_last_this = r0;
    s->aif_last_r1 = r1;
    if (s->rec_state != 0u) {
        d->rec_aif_calls++;
        d->rec_last_dfecore_this = r0;
    }
    if (r0 >= 0x60000000u && r0 < 0x80000000u) {
        s->aif_last_port = *(const volatile uint8_t *)(r0 + 8u);
        uint32_t sub = *(const volatile uint32_t *)(r0 + 0x0Cu);
        if (sub >= 0x60000000u && sub < 0x80000000u) {
            uint32_t hw_base = *(const volatile uint32_t *)(sub + 4u);
            uint32_t ch = *(const volatile uint8_t *)(sub + 8u);
            if ((hw_base & 0xFF000000u) == 0xFC000000u && ch < 16u) {
                uint32_t reg_va = hw_base + 0x200u + (ch << 7);
                if (s->rec_state != 0u || d->dfe_port8_rec_valid == 0u) {
                    copy_mmio32(d->dfe_port8_regs, reg_va, 20u);
                    d->dfe_port8_sub_ch = (hw_base & 0xFFFF0000u) | ch;
                    if (s->rec_state != 0u) {
                        d->dfe_port8_rec_valid = 1u;
                        for (uint32_t c = 0; c < 8u; ++c) {
                            uint32_t cv = hw_base + 0x200u + (c << 7);
                            uint32_t cu = *(const volatile uint32_t *)(cv + 0x10u);
                            uint32_t pa = *(const volatile uint32_t *)(cv + 0x20u);
                            uint32_t sz = *(const volatile uint32_t *)(cv + 0x24u);
                            uint32_t lp = *(const volatile uint32_t *)(cv + 0x2Cu);
                            if (pa != 0u || cu != 0u || sz != 0u) {
                                d->dfe_a_ch_cu[c] = cu;
                                d->dfe_a_ch_pa[c] = pa;
                                d->dfe_a_ch_sz[c] = sz;
                                d->dfe_a_ch_lp[c] = lp;
                            }
                        }
                    }
                }
            }
        }
    }
    mem_barrier();
}

/* 记录 Ifmt::slot[4] */
void zvp_spy_ifmt_s4(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r0; (void)r1; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->ifmt_s4_calls++;
    if (s->rec_state != 0u) {
        DBG->rec_ifmt_s4_calls++;
    }
    mem_barrier();
}

/* 记录 Ifmt::slot[10] (+0x28) */
void zvp_spy_ifmt_s10(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r0; (void)r1; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->ifmt_s10_calls++;
    mem_barrier();
}

static inline void arm_w0_dma_slot(uint32_t slot_pa, uint32_t w, uint32_t h) {
    volatile uint32_t *w0 = (volatile uint32_t *)0xF3002380u;
    /* 拍照路径 (dbg_v9) 中 +0x00=2->3 (不含 0x800 4-slot 环形位)，+0x1C=0x1C0B */
    w0[0x00u / 4u] = 2u;
    w0[0x0Cu / 4u] = 0u;
    if (w0[0x1Cu / 4u] == 0u) {
        w0[0x1Cu / 4u] = 0x00001C0Bu;
    }
    w0[0x20u / 4u] = slot_pa;
    w0[0x24u / 4u] = w;
    w0[0x2Cu / 4u] = h - 1u;
    mem_barrier();
    /* 清零 RAM shadow 中的 0x800 后置位 bit0 启动 W0，防止 0x639E21E1 覆写回 0x802 */
    w0[0x00u / 4u] = 3u;
    ((void (*)(uint32_t))0x63D0D98Bu)(0x6413783Cu);
    mem_barrier();
}

/* 前置声明 */
static void snap_cam_ctrl(volatile uint32_t *dst);
static void snap_aif_bus(volatile uint32_t *dst);
static void snap_rdcw0_hw(volatile uint32_t *dst);

static void snap_head0_regs(volatile uint32_t dst[24]) {
    copy_mmio32(dst, 0xF3000400u, 22u);
    dst[22] = *(const volatile uint32_t *)0xF3000458u;
    dst[23] = *(const volatile uint32_t *)0xF3000478u;
}

static void snap_dcv_hw(zvp_craw_dbg_t *d) {
    d->dcv_hw_rec_arm[0]  = *(const volatile uint32_t *)0xF3000E00u;
    d->dcv_hw_rec_arm[1]  = *(const volatile uint32_t *)0xF3000E10u;
    d->dcv_hw_rec_arm[2]  = *(const volatile uint32_t *)0xF3000E30u;
    d->dcv_hw_rec_arm[3]  = *(const volatile uint32_t *)0xF3000E34u;
    d->dcv_hw_rec_arm[4]  = *(const volatile uint32_t *)0xF3000E4Cu;
    d->dcv_hw_rec_arm[5]  = *(const volatile uint32_t *)0xF3000F00u;
    d->dcv_hw_rec_arm[6]  = *(const volatile uint32_t *)0xF3000F10u;
    d->dcv_hw_rec_arm[7]  = *(const volatile uint32_t *)0xF3000F40u;
    d->dcv_hw_rec_arm[8]  = *(const volatile uint32_t *)0xF3013000u;
    d->dcv_hw_rec_arm[9]  = *(const volatile uint32_t *)0xF3013080u;
    d->dcv_hw_rec_arm[10] = *(const volatile uint32_t *)0xF3000148u;
    d->dcv_hw_rec_arm[11] = *(const volatile uint32_t *)0xF300014Cu;
    d->dcv_hw_rec_arm[12] = *(const volatile uint32_t *)0xF30001A8u;
    d->dcv_hw_rec_arm[13] = *(const volatile uint32_t *)0xF30001D8u;
    d->dcv_hw_rec_arm[14] = *(const volatile uint32_t *)0xF3000F08u;
    d->dcv_hw_rec_arm[15] = *(const volatile uint32_t *)0xF3000F20u;
    d->dcv_hw_rec_fregs[0] = *(const volatile uint32_t *)0xF3000F28u;
    d->dcv_hw_rec_fregs[1] = *(const volatile uint32_t *)0xF3000F2Cu;
    d->dcv_hw_rec_fregs[2] = *(const volatile uint32_t *)0xF3000F30u;
    d->dcv_hw_rec_fregs[3] = *(const volatile uint32_t *)0xF3000F34u;
    d->dcv_hw_rec_fregs[4] = *(const volatile uint32_t *)0xF3000F38u;
    d->dcv_hw_rec_fregs[5] = *(const volatile uint32_t *)0xF3000F3Cu;
    d->dcv_hw_rec_fregs[6] = *(const volatile uint32_t *)0xF3000904u;
    d->dcv_hw_rec_fregs[7] = *(const volatile uint32_t *)0xF3000980u;
    d->dcv_e2c = *(const volatile uint32_t *)0xF3000E2Cu;
    d->dcv_e3c = *(const volatile uint32_t *)0xF3000E3Cu;
    d->rdc_10018 = *(const volatile uint32_t *)0xF3010018u;
}

static void w0_diag_refresh(zvp_craw_dbg_t *d) {
    snap_cam_ctrl(d->cam_ctrl_rec);
    snap_head0_regs(d->w0_head_regs);
    snap_aif_bus(d->aif_bus_rec_arm);
    snap_rdcw0_hw(d->rdcw0_hw_rec_arm);
    snap_dcv_hw(d);
}

/* 仅保活 HEAD0 W0 输出支路（不写入 TOP0 0x408，不触碰 CamDcv 与 Rdc） */
static void w0_ensure_head_only(void) {
    uint32_t h40c = *(const volatile uint32_t *)0xF300040Cu;
    if (h40c == 0u || h40c == 3u) {
        *(volatile uint32_t *)0xF300040Cu = 1u;
    }
    if ((*(const volatile uint32_t *)0xF3000408u & 1u) != 0u) {
        *(volatile uint32_t *)0xF3000408u &= ~1u;
    }
    if ((*(const volatile uint32_t *)0xF3000478u & 1u) == 0u) {
        *(volatile uint32_t *)0xF3000478u |= 1u;
    }
    mem_barrier();
}

/* 放行 CamDcv W0 裁剪与输出路由（在 RdcW0 旁路与 W0 DMA 已就绪后调用） */
static void w0_ensure_dcv_open(void) {
    if (*(const volatile uint32_t *)0xF3013000u == 0u) {
        *(volatile uint32_t *)0xF3013000u = 1u;
    }
    if (*(const volatile uint32_t *)0xF3000E00u == 0u) {
        *(volatile uint32_t *)0xF3000E00u = 1u;
    }
    if (*(const volatile uint32_t *)0xF3000F00u == 0u) {
        *(volatile uint32_t *)0xF3000F00u = 1u;
    }
    uint32_t need_e_trig = 0u;
    uint32_t need_f_trig = 0u;
    if (*(const volatile uint32_t *)0xF3000E30u != 0u) {
        *(volatile uint32_t *)0xF3000E30u = 0u;
        need_e_trig = 1u;
    }
    uint32_t e_win = *(const volatile uint32_t *)0xF3000E2Cu;
    if (e_win == 0u) {
        e_win = *(const volatile uint32_t *)0xF3000E3Cu;
    }
    if (*(const volatile uint32_t *)0xF3000E34u == 0u && e_win != 0u) {
        *(volatile uint32_t *)0xF3000E34u = e_win;
        need_e_trig = 1u;
    }
    if (*(const volatile uint32_t *)0xF3013080u != 0u) {
        *(volatile uint32_t *)0xF3013080u = 0u;
        need_f_trig = 1u;
    }
    if (*(const volatile uint32_t *)0xF3000E4Cu == 0u) {
        *(volatile uint32_t *)0xF3000E4Cu = 1u;
        need_e_trig = 1u;
    }
    if (*(const volatile uint32_t *)0xF3000F40u == 0u) {
        *(volatile uint32_t *)0xF3000F40u = 1u;
        need_f_trig = 1u;
    }
    mem_barrier();
    if ((need_e_trig != 0u || need_f_trig != 0u) &&
        (*(const volatile uint32_t *)0xF3000E10u & 1u) == 0u) {
        *(volatile uint32_t *)0xF3000E10u = 1u;
        mem_barrier();
    }
    if ((need_e_trig != 0u || need_f_trig != 0u) && *(const volatile uint32_t *)0xF3000F2Cu != 0u &&
        (*(const volatile uint32_t *)0xF3000F10u & 1u) == 0u) {
        *(volatile uint32_t *)0xF3000F10u = 1u;
        mem_barrier();
    }
}

/* 保持 RdcTop 使能与 RdcW0 旁路通畅（避免干扰 W1） */
static void w0_ensure_rdc_bypass(void) {
    uint32_t rdc118 = *(const volatile uint32_t *)0xF3010118u;
    uint32_t rdc178 = *(const volatile uint32_t *)0xF3010178u;
    if ((rdc178 & 0x200u) == 0u ||
        (rdc118 & 0x200u) == 0u ||
        (rdc178 & 0x38u) != 0u ||
        (rdc118 & 0x38u) != 0u ||
        *(const volatile uint32_t *)0xF3010000u == 0u ||
        *(const volatile uint32_t *)0xF3010018u == 0u ||
        *(const volatile uint32_t *)0xF301000Cu != 0u ||
        *(const volatile uint32_t *)0xF3010404u == 0u ||
        *(const volatile uint32_t *)0xF3010504u == 0u ||
        *(const volatile uint32_t *)0xF3010600u == 0u ||
        *(const volatile uint32_t *)0xF3010004u == 0u) {
        /* 仅当 W1 未启用压缩且 W1 Rdc 旁路未显式置位时，预先设为旁路以防开启 RdcTop 后拦截 W1 */
        if (*(const volatile uint32_t *)0xF3010010u == 0u &&
            *(const volatile uint32_t *)0xF3010704u == 0u) {
            *(volatile uint32_t *)0xF3010704u = 1u;
            *(volatile uint32_t *)0xF301011Cu = 0x40u;
            *(volatile uint32_t *)0xF301017Cu = 0x40u;
        }
        /* W1C 清除 RdcW0 (bit3..5=0x38) 压缩时钟并置 404/504/600=1 旁路，不触碰 W1 bit6 */
        *(volatile uint32_t *)0xF3010404u = 1u;
        *(volatile uint32_t *)0xF3010504u = 1u;
        *(volatile uint32_t *)0xF3010600u = 1u;
        *(volatile uint32_t *)0xF301011Cu = 0x38u;
        *(volatile uint32_t *)0xF301017Cu = 0x38u;
        *(volatile uint32_t *)0xF301000Cu = 0u;
        /* 按原厂 0x63D0E0D6(Rdc, 1) 开启 RdcTop 主时钟 (0x200 W1S) 与使能 */
        *(volatile uint32_t *)0xF3010178u = 0x200u;
        *(volatile uint32_t *)0xF3010118u = 0x200u;
        *(volatile uint32_t *)0xF3010018u = 1u;
        *(volatile uint32_t *)0xF3010000u = 1u;
        mem_barrier();
        *(volatile uint32_t *)0xF3010004u = 0u;
        mem_barrier();
        *(volatile uint32_t *)0xF3010004u = 1u;
        mem_barrier();
    }
}

static void w0_raw_disarm(zvp_craw_shm_t *s, zvp_craw_dbg_t *d) {
    if (d->dfe_ch_pa[3] == 0x5730u) {
        uint32_t active_stg = d->dfe_ch_pa[5];
        volatile uint32_t *w0 = (volatile uint32_t *)0xF3002380u;
        /* 按“上游 -> 下游”对称逆序关闭：Stage 3 (HEAD0) -> Stage 4 (CamDcv若已开) -> Stage 2 (W0 DMA + Rdc + CamAif) */
        if (active_stg >= 3u) {
            *(volatile uint32_t *)0xF3000478u = d->w0_raw_backup[2];
            *(volatile uint32_t *)0xF3000408u = d->w0_raw_backup[0];
            *(volatile uint32_t *)0xF300040Cu = d->w0_raw_backup[1];
            mem_barrier();
        }
        /* 仅当 Stage 4 确实按需开启过 CamDcv 时才还原其路由位与 E30/E34 裁剪窗 */
        if (active_stg >= 4u) {
            *(volatile uint32_t *)0xF3000E30u = d->dcv_bak_e30;
            *(volatile uint32_t *)0xF3000E34u = d->dcv_bak_e34;
            *(volatile uint32_t *)0xF3000F40u = d->dcv_raw_backup[9];
            *(volatile uint32_t *)0xF3000E4Cu = d->dcv_raw_backup[7];
            *(volatile uint32_t *)0xF3013080u = d->dcv_raw_backup[11];
            mem_barrier();
            if ((*(const volatile uint32_t *)0xF3000E10u & 1u) == 0u) {
                *(volatile uint32_t *)0xF3000E10u = 1u;
            }
            if (*(const volatile uint32_t *)0xF3000F2Cu != 0u &&
                (*(const volatile uint32_t *)0xF3000F10u & 1u) == 0u) {
                *(volatile uint32_t *)0xF3000F10u = 1u;
            }
            *(volatile uint32_t *)0xF3000F00u = d->dcv_raw_backup[8];
            *(volatile uint32_t *)0xF3013000u = d->dcv_raw_backup[10];
            *(volatile uint32_t *)0xF3000E00u = d->dcv_raw_backup[6];
            mem_barrier();
        }
        if (active_stg >= 2u) {
            for (uint32_t i = 0u; i < 4096u; ++i) {
                if ((w0[0x04u / 4u] & 0x80000000u) == 0u) break;
            }
            w0[0x00u / 4u] &= ~1u;
            mem_barrier();
            if (d->rec_rdc_at_arm[4] == 0u && d->rec_rdc_at_arm[3] == 0u) {
                *(volatile uint32_t *)0xF3010704u = 0u;
            }
            *(volatile uint32_t *)0xF3010600u = d->dcv_raw_backup[15];
            *(volatile uint32_t *)0xF3010504u = d->dcv_raw_backup[14];
            *(volatile uint32_t *)0xF3010404u = d->dcv_raw_backup[13];
            *(volatile uint32_t *)0xF301000Cu = d->w0_raw_backup[5];
            *(volatile uint32_t *)0xF3010018u = d->w0_raw_backup[10];
            *(volatile uint32_t *)0xF3010000u = d->w0_raw_backup[9];
            *(volatile uint32_t *)0xF3010004u = d->dcv_raw_backup[12];
            if ((d->w0_raw_backup[7] & 0x38u) != 0u) {
                *(volatile uint32_t *)0xF3010118u = (d->w0_raw_backup[7] & 0x38u);
            }
            if ((d->w0_raw_backup[8] & 0x38u) != 0u) {
                *(volatile uint32_t *)0xF3010178u = (d->w0_raw_backup[8] & 0x38u);
            }
            if ((d->w0_raw_backup[7] & 0x200u) == 0u) {
                *(volatile uint32_t *)0xF301011Cu = 0x200u;
            }
            if ((d->w0_raw_backup[8] & 0x200u) == 0u) {
                *(volatile uint32_t *)0xF301017Cu = 0x200u;
            }
            uint32_t top0_clr = 0u;
            if ((d->w0_raw_backup[6] & 0x8u) == 0u) top0_clr |= 0x8u;
            if ((d->dcv_raw_backup[0] & 0x400u) == 0u) top0_clr |= 0x400u;
            if (top0_clr != 0u) {
                *(volatile uint32_t *)0xF300000Cu = top0_clr;
                *(volatile uint32_t *)0xF3000004u = top0_clr;
            }
            mem_barrier();
            ((void (*)(uint32_t))0x639E3979u)(3u); /* pbl_aif reset/close port 3 */
            *(volatile uint32_t *)0xF2804114u = d->w0_raw_backup[3];
            *(volatile uint32_t *)0xF2807064u = d->w0_raw_backup[4];
            mem_barrier();
        }
        d->dfe_ch_pa[3] = 0u;
    }
    d->rec_w0_state = 0u;
    s->rec_state = 0u;
    mem_barrier();
}

/* Movie REC 期 W0 (HEAD0 -> RdcW0 Bypass -> Port 3 @ 0xF3002380) 真 Bayer RAW 源路由与轮询推进 */
static void w0_raw_rec_service(zvp_craw_shm_t *s, zvp_craw_dbg_t *d, uint32_t allow_wait) {
    if (s->rec_state == 0u || s->mode != ZVP_CRAW_LIRO_RAW_W0 ||
        s->ring_base_pa == 0u || s->frame_stride == 0u || s->slot_count <= 1u) {
        return;
    }
    volatile uint32_t *w0 = (volatile uint32_t *)0xF3002380u;

    /* 优先以 CamDcv 实流裁剪窗 (E34/E2C) 确定传感器有效尺寸，确保 W0 行宽/行数与上游 1:1 匹配 */
    uint32_t w = 0u;
    uint32_t h = 0u;
    uint32_t e_win = *(const volatile uint32_t *)0xF3000E34u;
    if (e_win == 0u) {
        e_win = *(const volatile uint32_t *)0xF3000E2Cu;
    }
    if (e_win == 0u) {
        e_win = *(const volatile uint32_t *)0xF3000E3Cu;
    }
    if (e_win != 0u) {
        uint32_t dw = ((e_win >> 16) & 0x3FFFu) + 1u;
        uint32_t dh = (e_win & 0x3FFFu) + 1u;
        if (dw >= 256u && dw <= 8192u && dh >= 128u && dh <= 6000u) {
            w = dw;
            h = dh;
        }
    }
    if (w < 256u || w > 8192u || h < 128u || h > 6000u) {
        uint32_t r43c = *(const volatile uint32_t *)0xF300043Cu;
        uint32_t hw = r43c & 0x3FFFu;
        uint32_t hh = (r43c >> 16) & 0x3FFFu;
        if (hw >= 256u && hw <= 8192u && hh >= 128u && hh <= 6000u) {
            w = hw;
            h = hh;
        } else {
            w = *(const volatile uint32_t *)0x641372D8u; /* CamHead +0x48 */
            h = *(const volatile uint32_t *)0x641372DCu; /* CamHead +0x4C */
            if (w < 256u || w > 8192u || h < 128u || h > 6000u) {
                w = (s->target_w >= 256u && s->target_w <= 8192u) ? s->target_w : 2008u;
                h = (s->target_h >= 128u && s->target_h <= 6000u) ? s->target_h : 1134u;
            }
        }
    }
    /* pbl_aif_set_func (0x639E3755) 严格校验 8 字节对齐，将行步长按 8 字节上对齐并保持偶数像素宽 */
    uint32_t byte_stride = ((w * 2u) + 7u) & ~7u;
    uint32_t w_aligned = byte_stride >> 1;
    if (byte_stride == 0u || byte_stride > s->frame_stride) return;
    while (byte_stride * h > s->frame_stride && h > 1u) {
        h--;
    }

    if (d->rec_w0_state == 1u) {
        uint32_t cur_slot = d->dfe_ch_pa[7];
        if (cur_slot >= s->slot_count) {
            cur_slot = 0u;
            d->dfe_ch_pa[7] = 0u;
        }
        s->target_w = w_aligned;
        s->target_h = h;
        s->target_enc_type = 1u; /* 16-bit linear uncompressed Bayer RAW */
        d->w0_raw_cfg_rc[2] = w_aligned;
        d->w0_raw_cfg_rc[3] = h;

        if (d->dfe_ch_pa[3] != 0x5730u) {
            copy_mmio32(d->rec_w0_at_arm, 0xF3002380u, 16u);
            d->w0_raw_backup[0]  = *(const volatile uint32_t *)0xF3000408u;
            d->w0_raw_backup[1]  = *(const volatile uint32_t *)0xF300040Cu;
            d->w0_raw_backup[2]  = *(const volatile uint32_t *)0xF3000478u;
            d->w0_raw_backup[3]  = *(const volatile uint32_t *)0xF2804114u;
            d->w0_raw_backup[4]  = *(const volatile uint32_t *)0xF2807064u;
            d->w0_raw_backup[5]  = *(const volatile uint32_t *)0xF301000Cu;
            d->w0_raw_backup[6]  = *(const volatile uint32_t *)0xF3000000u;
            d->w0_raw_backup[7]  = *(const volatile uint32_t *)0xF3010118u;
            d->w0_raw_backup[8]  = *(const volatile uint32_t *)0xF3010178u;
            d->w0_raw_backup[9]  = *(const volatile uint32_t *)0xF3010000u;
            d->w0_raw_backup[10] = *(const volatile uint32_t *)0xF3010018u;
            d->rec_rdc_at_arm[3] = *(const volatile uint32_t *)0xF3010704u;
            d->rec_rdc_at_arm[4] = *(const volatile uint32_t *)0xF3010010u;

            d->dcv_raw_backup[0]  = d->w0_raw_backup[6];
            d->dcv_raw_backup[1]  = *(const volatile uint32_t *)0xF3000008u;
            d->dcv_raw_backup[2]  = *(const volatile uint32_t *)0xF3000148u;
            d->dcv_raw_backup[3]  = *(const volatile uint32_t *)0xF300014Cu;
            d->dcv_raw_backup[4]  = *(const volatile uint32_t *)0xF30001A8u;
            d->dcv_raw_backup[5]  = *(const volatile uint32_t *)0xF30001D8u;
            d->dcv_raw_backup[6]  = *(const volatile uint32_t *)0xF3000E00u;
            d->dcv_raw_backup[7]  = *(const volatile uint32_t *)0xF3000E4Cu;
            d->dcv_raw_backup[8]  = *(const volatile uint32_t *)0xF3000F00u;
            d->dcv_raw_backup[9]  = *(const volatile uint32_t *)0xF3000F40u;
            d->dcv_raw_backup[10] = *(const volatile uint32_t *)0xF3013000u;
            d->dcv_raw_backup[11] = *(const volatile uint32_t *)0xF3013080u;
            d->dcv_raw_backup[12] = *(const volatile uint32_t *)0xF3010004u;
            d->dcv_raw_backup[13] = *(const volatile uint32_t *)0xF3010404u;
            d->dcv_raw_backup[14] = *(const volatile uint32_t *)0xF3010504u;
            d->dcv_raw_backup[15] = *(const volatile uint32_t *)0xF3010600u;
            d->dcv_bak_e30        = *(const volatile uint32_t *)0xF3000E30u;
            d->dcv_bak_e34        = *(const volatile uint32_t *)0xF3000E34u;

            /* Stage 1 (0..2.0s): 纯只读观察基线，绝不写任何 HEAD0/CamDcv/TOP0/W0/Rdc 寄存器 */
            d->dfe_ch_pa[3] = 0x5730u; /* 'W0' */
            d->dfe_ch_pa[4] = 1u;
            d->dfe_ch_pa[5] = 1u;
            d->dfe_ch_pa[6] = 0u;
            d->w0_raw_cfg_rc[0] = 0u;
            d->w0_raw_cfg_rc[1] = 0u;
            d->w0_raw_backup[11] = 0u;
            w0_diag_refresh(d);
        }
        d->rec_w0_state = 2u;
        d->rec_w0_last_ctrl = w0[0x00u / 4u];
        d->rec_w0_last_stat = w0[0x04u / 4u];
        d->rec_w0_last_cu = w0[0x10u / 4u];
        copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
        mem_barrier();
        return;
    }

    if (d->rec_w0_state == 2u) {
        /* 阶段推进完全由 RTOS 本地 dfe_ch_pa[6] 驱动，消除跨核 D-Cache 不一致导致跳过 Stage 2 的问题 */
        uint32_t cnt = d->dfe_ch_pa[6] + 1u;
        d->dfe_ch_pa[6] = cnt;
        uint32_t want_stg = 1u;
        if (cnt > 300u) {
            want_stg = 4u;
        } else if (cnt > 200u) {
            want_stg = 3u;
        } else if (cnt > 100u) {
            want_stg = 2u;
        }
        d->dfe_ch_pa[4] = want_stg;

        /* Stage 1 (0..100 ticks): 纯只读观察原厂开录时序，不写入任何硬件寄存器 */
        if (want_stg <= 1u) {
            d->dfe_ch_pa[5] = 1u;
            d->rec_w0_last_ctrl = w0[0x00u / 4u];
            d->rec_w0_last_stat = w0[0x04u / 4u];
            d->rec_w0_last_cu = w0[0x10u / 4u];
            copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
            w0_diag_refresh(d);
            mem_barrier();
            return;
        }

        /* Stage 2 (101..200 ticks 或尚无 active_stg>=2): 强制先开下游 RdcBypass + pbl_aif + W0 DMA，不动 HEAD0/CamDcv */
        if (want_stg == 2u || d->dfe_ch_pa[5] < 2u) {
            if (d->dfe_ch_pa[5] < 2u) {
                d->w0_raw_backup[3]  = *(const volatile uint32_t *)0xF2804114u;
                d->w0_raw_backup[4]  = *(const volatile uint32_t *)0xF2807064u;
                d->w0_raw_backup[5]  = *(const volatile uint32_t *)0xF301000Cu;
                d->w0_raw_backup[6]  = *(const volatile uint32_t *)0xF3000000u;
                d->w0_raw_backup[7]  = *(const volatile uint32_t *)0xF3010118u;
                d->w0_raw_backup[8]  = *(const volatile uint32_t *)0xF3010178u;
                d->w0_raw_backup[9]  = *(const volatile uint32_t *)0xF3010000u;
                d->w0_raw_backup[10] = *(const volatile uint32_t *)0xF3010018u;
                d->rec_rdc_at_arm[3] = *(const volatile uint32_t *)0xF3010704u;
                d->rec_rdc_at_arm[4] = *(const volatile uint32_t *)0xF3010010u;
                d->dcv_raw_backup[0] = d->w0_raw_backup[6];
                d->dcv_raw_backup[12] = *(const volatile uint32_t *)0xF3010004u;
                d->dcv_raw_backup[13] = *(const volatile uint32_t *)0xF3010404u;
                d->dcv_raw_backup[14] = *(const volatile uint32_t *)0xF3010504u;
                d->dcv_raw_backup[15] = *(const volatile uint32_t *)0xF3010600u;
                w0_ensure_rdc_bypass();

                uint32_t cur_slot = d->dfe_ch_pa[7];
                if (cur_slot >= s->slot_count) {
                    cur_slot = 0u;
                    d->dfe_ch_pa[7] = 0u;
                }
                uint32_t slot_pa = s->ring_base_pa + cur_slot * s->frame_stride;
                for (uint32_t i = 0u; i < 0x20000u; ++i) {
                    if ((w0[0x04u / 4u] & 0x80000000u) == 0u) break;
                }
                if ((w0[0x04u / 4u] & 0x80000000u) == 0u) {
                    ((void (*)(uint32_t))0x639E226Du)(3u);
                    *(volatile uint32_t *)0xF2804114u = 0x00000477u;

                    uint8_t fp[20];
                    for (uint32_t i = 0u; i < sizeof(fp); ++i) fp[i] = 0u;
                    fp[0] = 1u;
                    fp[5] = 0u;
                    *(uint32_t *)(void *)(fp + 8u)  = slot_pa;
                    *(uint32_t *)(void *)(fp + 12u) = byte_stride;
                    int rc_fn = ((int (*)(uint32_t, const void *))0x639E3755u)(3u, fp);

                    uint8_t rp[24];
                    for (uint32_t i = 0u; i < sizeof(rp); ++i) rp[i] = 0u;
                    *(uint16_t *)(void *)(rp + 0u) = (uint16_t)(h - 1u);
                    int rc_rc = ((int (*)(uint32_t, const void *))0x639E3879u)(3u, rp);
                    d->w0_raw_cfg_rc[0] = (uint32_t)rc_fn;
                    d->w0_raw_cfg_rc[1] = (uint32_t)rc_rc;

                    ((void (*)(uint32_t))0x63D0A877u)(0x6413790Cu);
                    d->w0_raw_backup[11] = ((uint32_t (*)(uint32_t))0x63DA20F7u)(3u);

                    w0[0x1Cu / 4u] = 0x00001C0Bu;
                    w0[0x08u / 4u] = 0u;
                    ((void (*)(uint32_t))0x639E2385u)(3u);
                    w0[0x00u / 4u] = 2u;
                    w0[0x20u / 4u] = slot_pa;
                    w0[0x24u / 4u] = byte_stride;
                    w0[0x2Cu / 4u] = h - 1u;
                    mem_barrier();
                    w0[0x00u / 4u] = 3u;
                    mem_barrier();
                    d->rec_w0_cur_pa = slot_pa;
                    d->rec_w0_arm_cnt++;
                    d->dfe_ch_pa[5] = 2u;
                }
            } else {
                w0_ensure_rdc_bypass();
            }
            d->rec_w0_last_ctrl = w0[0x00u / 4u];
            d->rec_w0_last_stat = w0[0x04u / 4u];
            d->rec_w0_last_cu = w0[0x10u / 4u];
            copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
            w0_diag_refresh(d);
            mem_barrier();
            return;
        }

        /* 共享推进 W0 单帧 DMA 环形槽：未完成一帧（ctrl bit0=1 且 cu 未过 cur_pa）时不打断 */
        /* Stage 3 (201..300 ticks 或尚无 active_stg>=3): 下游 Rdc+W0 已就绪后开启 HEAD0（不触碰 CamDcv）并推进 W0 环 */
        if (want_stg == 3u || d->dfe_ch_pa[5] < 3u) {
            w0_ensure_rdc_bypass();
            if (d->dfe_ch_pa[5] < 3u) {
                d->w0_raw_backup[0] = *(const volatile uint32_t *)0xF3000408u;
                d->w0_raw_backup[1] = *(const volatile uint32_t *)0xF300040Cu;
                d->w0_raw_backup[2] = *(const volatile uint32_t *)0xF3000478u;
                w0_ensure_head_only();
                d->dfe_ch_pa[5] = 3u;
                d->rec_w0_last_ctrl = w0[0x00u / 4u];
                d->rec_w0_last_stat = w0[0x04u / 4u];
                d->rec_w0_last_cu = w0[0x10u / 4u];
                copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
                w0_diag_refresh(d);
                mem_barrier();
                return;
            }
            w0_ensure_head_only();
        } else {
            /* Stage 4 (3.3s+, want_stg >= 4): 若 Stage 3 尚未成功收满任何帧 (done==0)，才按需备份并开启 CamDcv */
            w0_ensure_rdc_bypass();
            if (d->dfe_ch_pa[5] < 3u) {
                d->w0_raw_backup[0] = *(const volatile uint32_t *)0xF3000408u;
                d->w0_raw_backup[1] = *(const volatile uint32_t *)0xF300040Cu;
                d->w0_raw_backup[2] = *(const volatile uint32_t *)0xF3000478u;
                d->dfe_ch_pa[5] = 3u;
            }
            w0_ensure_head_only();
            if (d->dfe_ch_pa[5] < 4u && d->rec_w0_done_cnt == 0u) {
                d->dcv_raw_backup[6]  = *(const volatile uint32_t *)0xF3000E00u;
                d->dcv_raw_backup[7]  = *(const volatile uint32_t *)0xF3000E4Cu;
                d->dcv_raw_backup[8]  = *(const volatile uint32_t *)0xF3000F00u;
                d->dcv_raw_backup[9]  = *(const volatile uint32_t *)0xF3000F40u;
                d->dcv_raw_backup[10] = *(const volatile uint32_t *)0xF3013000u;
                d->dcv_raw_backup[11] = *(const volatile uint32_t *)0xF3013080u;
                d->dcv_bak_e30        = *(const volatile uint32_t *)0xF3000E30u;
                d->dcv_bak_e34        = *(const volatile uint32_t *)0xF3000E34u;
                w0_ensure_dcv_open();
                d->dfe_ch_pa[5] = 4u;
                d->rec_w0_last_ctrl = w0[0x00u / 4u];
                d->rec_w0_last_stat = w0[0x04u / 4u];
                d->rec_w0_last_cu = w0[0x10u / 4u];
                copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
                w0_diag_refresh(d);
                mem_barrier();
                return;
            }
            if (d->dfe_ch_pa[5] >= 4u) {
                w0_ensure_dcv_open();
            }
        }

        uint32_t stat = w0[0x04u / 4u];
        if ((stat & 0x80000000u) != 0u) {
            if (allow_wait != 0u) {
                for (uint32_t spin = 0u; spin < 256u; ++spin) {
                    stat = w0[0x04u / 4u];
                    if ((stat & 0x80000000u) == 0u) break;
                }
            }
            if ((stat & 0x80000000u) != 0u) {
                d->rec_w0_last_ctrl = w0[0x00u / 4u];
                d->rec_w0_last_stat = stat;
                d->rec_w0_last_cu = w0[0x10u / 4u];
                copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
                w0_diag_refresh(d);
                mem_barrier();
                return;
            }
        }

        uint32_t cu = w0[0x10u / 4u];
        uint32_t ctrl = w0[0x00u / 4u];
        uint32_t cur_pa = d->rec_w0_cur_pa;
        if ((ctrl & 1u) != 0u && cu <= cur_pa) {
            /* 仍在等当前帧写入完成，不可中途重臂覆盖当前槽 */
            d->rec_w0_last_ctrl = ctrl;
            d->rec_w0_last_stat = stat;
            d->rec_w0_last_cu = cu;
            copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
            w0_diag_refresh(d);
            mem_barrier();
            return;
        }
        uint32_t cur_slot = d->dfe_ch_pa[7];
        d->rec_w0_last_ctrl = ctrl;
        d->rec_w0_last_stat = stat;
        d->rec_w0_last_cu = cu;
        copy_mmio32(d->rec_w0_at_tick, 0xF3002380u, 16u);
        w0_diag_refresh(d);

        if (cu > cur_pa) {
            s->last_ring_pa_written = cu;
            cur_slot++;
            if (cur_slot >= s->slot_count) {
                cur_slot = 0u;
            }
            d->dfe_ch_pa[7] = cur_slot;
            uint32_t wseq = s->write_seq;
            uint32_t rseq = s->read_seq;
            if ((wseq - rseq) < (s->slot_count - 1u)) {
                s->write_seq = wseq + 1u;
            } else {
                s->drop_seq++;
            }
            d->rec_w0_done_cnt++;
        } else {
            d->rec_w0_err_cnt++;
        }

        uint32_t next_pa = s->ring_base_pa + cur_slot * s->frame_stride;
        ((void (*)(uint32_t))0x639E2385u)(3u);
        w0[0x00u / 4u] = 2u;
        w0[0x20u / 4u] = next_pa;
        w0[0x24u / 4u] = byte_stride;
        w0[0x2Cu / 4u] = h - 1u;
        mem_barrier();
        w0[0x00u / 4u] = 3u;
        mem_barrier();
        d->rec_w0_cur_pa = next_pa;
        d->rec_w0_arm_cnt++;
        mem_barrier();
    }
}

/* Movie REC 期 Port 4 (W1 @ 0xF3002400) 安全接管与多槽环形推进（Busy==0 门控防总线挂死） */
static void w1_rec_service(zvp_craw_shm_t *s, zvp_craw_dbg_t *d, uint32_t allow_wait) {
    if (s->rec_state == 0u || s->mode != ZVP_CRAW_LIRO_ARM_2K ||
        s->ring_base_pa == 0u || s->frame_stride == 0u || s->slot_count <= 1u) {
        return;
    }
    volatile uint32_t *w1 = (volatile uint32_t *)0xF3002400u;
    if (d->rec_w0_state == 1u) {
        uint32_t cur_slot = d->dfe_ch_pa[7];
        if (cur_slot >= s->slot_count) {
            cur_slot = 0u;
            d->dfe_ch_pa[7] = 0u;
        }
        uint32_t slot_pa = s->ring_base_pa + cur_slot * s->frame_stride;
        /* 仅在首帧准备时抓拍 W1 原始配置用于停录精准还原 */
        if (d->dfe_ch_pa[3] != 0x5731u) {
            copy_mmio32(d->rec_w0_at_arm, 0xF3002400u, 16u);
            snap_aif_bus(d->aif_bus_rec_arm);
            snap_rdcw0_hw(d->rdcw0_hw_rec_arm);
            d->dfe_ch_pa[0] = w1[0x00u / 4u];
            d->dfe_ch_pa[1] = w1[0x20u / 4u];
            d->dfe_ch_pa[2] = w1[0x24u / 4u];
            d->dfe_ch_pa[3] = 0x5731u; /* 'W1' */
            d->rec_rdc_at_arm[0] = w1[0x28u / 4u];
            d->rec_rdc_at_arm[1] = w1[0x2Cu / 4u];
            d->rec_rdc_at_arm[2] = *(const volatile uint32_t *)0xF3010700u;
            d->rec_rdc_at_arm[3] = *(const volatile uint32_t *)0xF3010704u;
            d->rec_rdc_at_arm[4] = *(const volatile uint32_t *)0xF3010714u;
            d->rec_rdc_at_arm[5] = *(const volatile uint32_t *)0xF3010720u;
            d->rec_rdc_at_arm[6] = *(const volatile uint32_t *)0xF3000C04u;
            d->rec_rdc_at_arm[7] = *(const volatile uint32_t *)0xF3000C30u;
            d->dfe_ch_sz[0] = *(const volatile uint32_t *)(0x6413793Cu + 0x24u);
            d->dfe_ch_sz[1] = *(const volatile uint32_t *)(0x6413793Cu + 0x28u);
            if (d->dfe_ch_sz[1] >= 0x60000000u && d->dfe_ch_sz[1] < 0x68000000u) {
                d->dfe_ch_sz[2] = *(const volatile uint32_t *)(d->dfe_ch_sz[1]);
            }
        }
        uint32_t orig_ctrl = d->dfe_ch_pa[0];
        if (allow_wait && (w1[0x04u / 4u] & 0x80000000u) != 0u) {
            w1[0x00u / 4u] = orig_ctrl & ~1u;
            mem_barrier();
            for (uint32_t k = 0u; k < 65536u && (w1[0x04u / 4u] & 0x80000000u) != 0u; ++k) {
            }
        }
        if ((w1[0x04u / 4u] & 0x80000000u) == 0u) {
            ((void (*)(uint32_t))0x639E2385u)(4u);
            w1[0x20u / 4u] = slot_pa;
            mem_barrier();
            w1[0x00u / 4u] = (orig_ctrl & ~0x800u) | 1u;
            mem_barrier();
            d->rec_w0_cur_pa = slot_pa;
            d->rec_w0_state = 2u;
            d->rec_w0_arm_cnt++;
        } else {
            d->dfe_ch_pa[6]++; /* busy skip */
        }
        mem_barrier();
        return;
    }

    if (d->rec_w0_state == 2u) {
        uint32_t ctrl   = w1[0x00u / 4u];
        uint32_t stat   = w1[0x04u / 4u];
        uint32_t cu     = w1[0x10u / 4u];
        uint32_t base20 = w1[0x20u / 4u];
        d->rec_w0_last_ctrl = ctrl;
        d->rec_w0_last_stat = stat;
        d->rec_w0_last_cu   = cu;
        copy_mmio32(d->rec_w0_at_tick, 0xF3002400u, 16u);
        uint32_t ring_end = s->ring_base_pa + s->ring_total_bytes;
        if (cu >= s->ring_base_pa && cu < ring_end) {
            s->last_ring_pa_written = cu;
        }

        uint32_t orig_ctrl = d->dfe_ch_pa[0];
        uint32_t run_ctrl  = ((ctrl != 0u ? ctrl : orig_ctrl) & ~0x800u) | 1u;
        if (base20 != d->rec_w0_cur_pa || (ctrl & 0x800u) != 0u) {
            /* 原厂将 W1 拉回 0xFF000000/Discard：仅在 Busy==0 时安全夺回 */
            d->rec_w0_err_cnt++;
            if (allow_wait && (w1[0x04u / 4u] & 0x80000000u) != 0u) {
                w1[0x00u / 4u] = ctrl & ~1u;
                mem_barrier();
                for (uint32_t k = 0u; k < 65536u && (w1[0x04u / 4u] & 0x80000000u) != 0u; ++k) {
                }
            }
            if ((w1[0x04u / 4u] & 0x80000000u) == 0u) {
                ((void (*)(uint32_t))0x639E2385u)(4u);
                w1[0x20u / 4u] = d->rec_w0_cur_pa;
                mem_barrier();
                w1[0x00u / 4u] = run_ctrl;
                mem_barrier();
                d->rec_w0_arm_cnt++;
            } else {
                if (allow_wait) {
                    w1[0x00u / 4u] = ctrl;
                    mem_barrier();
                }
                d->dfe_ch_pa[6]++;
            }
        } else if (cu > d->rec_w0_cur_pa && cu < ring_end && (stat & 0x80000000u) == 0u) {
            /* 上一帧已完全结束 (Busy==0)：统计帧长并推进多槽环形 write_seq 唤醒 drainer 写卡 */
            uint32_t fbytes = cu - d->rec_w0_cur_pa;
            if (d->dfe_ch_pa[4] == 0u || fbytes < d->dfe_ch_pa[4]) {
                d->dfe_ch_pa[4] = fbytes;
            }
            if (fbytes > d->dfe_ch_pa[5]) {
                d->dfe_ch_pa[5] = fbytes;
            }
            d->rec_w0_done_cnt++;

            uint32_t used = s->write_seq - s->read_seq;
            uint32_t next_pa = d->rec_w0_cur_pa;
            if (used + 1u < s->slot_count) {
                uint32_t next_slot = d->dfe_ch_pa[7] + 1u;
                if (next_slot >= s->slot_count) {
                    next_slot = 0u;
                }
                uint32_t next_seq = s->write_seq + 1u;
                d->dfe_ch_pa[7] = next_slot;
                mem_barrier();
                s->write_seq = next_seq;
                next_pa = s->ring_base_pa + next_slot * s->frame_stride;
            } else {
                /* 环将满，保留 read_seq 所在槽不被覆盖并原地复用 */
                s->drop_seq++;
            }

            w1[0x00u / 4u] = ctrl & ~1u;
            mem_barrier();
            ((void (*)(uint32_t))0x639E2385u)(4u);
            w1[0x20u / 4u] = next_pa;
            mem_barrier();
            w1[0x00u / 4u] = run_ctrl;
            mem_barrier();
            d->rec_w0_cur_pa = next_pa;
            d->rec_w0_arm_cnt++;
        }
    }
    mem_barrier();
}

/* RdcW0::slot[7] (已知严格仅用 r0=this, r1=cfg[3]) */
typedef void (*fn_rdcw0_s7_t)(void *self, const uint32_t *cfg);
void hook_rdcw0_s7(void *self, uint32_t *cfg) {
    zvp_craw_shm_t *s = SHM;
    const uint32_t *use_cfg = cfg;
    uint32_t override_cfg[3];

    if (s->magic == ZVP_CRAW_SHM_MAGIC && s->mode != ZVP_CRAW_LIRO_BYPASS) {
        s->rdc_calls++;
        s->rdc_last_this = (uint32_t)self;
        s->rdc_last_lr   = (uint32_t)__builtin_return_address(0);
        if (cfg) {
            s->rdc_last_enc = cfg[0];
            s->rdc_last_w = cfg[1];
            s->rdc_last_h = cfg[2];
        }
        zvp_craw_dbg_t *d = DBG;
        ensure_dbg_init(d);
        if (cfg) {
            for (uint32_t i = 0; i < 6u; ++i) {
                d->rdc_cfg_words[i] = cfg[i];
            }
        }
        /* 若为 Core 0 RdcW0 (0x6413783c)，抓取 W0 (0xF3002380) 与 R0 (0xF3002200) 全寄存器组 */
        if ((uint32_t)self == 0x6413783Cu) {
            copy_mmio32(d->w0_at_rdc, 0xF3002380u, 32u);
            copy_mmio32(d->r0_at_rdc, 0xF3002200u, 32u);
            d->w0_at_rdc_valid = 1u;
            s->last_ring_pa_written = d->w0_at_rdc[4]; /* +0x10 = cuAdrs */
            s->dfe_rdcw0_last_cfg   = d->w0_at_rdc[11]; /* +0x2C = loop */
        }
        /* 静照阶段保持原厂传感器全幅尺寸，避免与 Ifmt 不匹配；仅录像期才覆盖 2K RDC */
        if (s->mode == ZVP_CRAW_LIRO_ARM_2K && s->rec_state != 0u &&
            (s->flags & ZVP_CRAW_FLAG_OVERRIDE_RDC) && cfg) {
            override_cfg[0] = s->target_enc_type ? s->target_enc_type : cfg[0];
            override_cfg[1] = s->target_w ? s->target_w : cfg[1];
            override_cfg[2] = s->target_h ? s->target_h : cfg[2];
            use_cfg = override_cfg;
        }
        mem_barrier();
    }
    ((fn_rdcw0_s7_t)(ZVP_AVCAM_ORIG_RDCW0_S7))(self, use_cfg);
    if (s->magic == ZVP_CRAW_SHM_MAGIC && s->mode != ZVP_CRAW_LIRO_BYPASS && (uint32_t)self == 0x6413783Cu) {
        zvp_craw_dbg_t *d = DBG;
        if (s->rec_state == 0u) {
            snap_cam_ctrl(d->cam_ctrl_still);
            snap_aif_bus(d->aif_bus_still);
            snap_rdcw0_hw(d->rdcw0_hw_still);
        }
        if (s->mode == ZVP_CRAW_LIRO_ARM_2K && s->ring_base_pa != 0u && s->rec_state == 0u) {
            volatile uint32_t *w0_pa_reg = (volatile uint32_t *)(0xF3002380u + 0x20u);
            d->w0_orig_start_pa = *w0_pa_reg;
            *w0_pa_reg = s->ring_base_pa;
            mem_barrier();
            d->w0_redirect_pa = *w0_pa_reg;
            d->w0_redirect_cnt++;
            s->last_ring_pa_written = d->w0_redirect_pa;
        }
        copy_mmio32(d->w0_after_rdc, 0xF3002380u, 32u);
        copy_mmio32(d->w1_at_idle, 0xF3002400u, 16u);
        copy_mmio32(d->w2_at_idle, 0xF3002480u, 16u);
        d->w0_after_rdc_valid = 1u;
        mem_barrier();
    }
}

static void snap_cam_ctrl(volatile uint32_t *dst) {
    dst[0]  = *(const volatile uint32_t *)0xF3000000u; /* TOP0 ctrl */
    dst[1]  = *(const volatile uint32_t *)0xF3000028u; /* TOP0 bussel */
    dst[2]  = *(const volatile uint32_t *)0xF3000118u;
    dst[3]  = *(const volatile uint32_t *)0xF300011Cu;
    dst[4]  = *(const volatile uint32_t *)0xF3000150u;
    dst[5]  = *(const volatile uint32_t *)0xF3000178u;
    dst[6]  = *(const volatile uint32_t *)0xF300017Cu;
    dst[7]  = *(const volatile uint32_t *)0xF3000408u; /* HEAD0 */
    dst[8]  = *(const volatile uint32_t *)0xF3000410u; /* HEAD0 status */
    dst[9]  = *(const volatile uint32_t *)0xF3000478u; /* TOP0/HEAD */
    dst[10] = *(const volatile uint32_t *)0xF3000684u; /* SUB size */
    dst[11] = *(const volatile uint32_t *)0xF3000904u; /* CAS0 */
    dst[12] = *(const volatile uint32_t *)0xF3000C00u; /* DEV0 ctrl */
    dst[13] = *(const volatile uint32_t *)0xF3000C04u; /* DEV0 mux (0x63775dd4) */
    dst[14] = *(const volatile uint32_t *)0xF3000C10u; /* DEV0 status */
    dst[15] = *(const volatile uint32_t *)0xF3000C30u; /* DEV0 crop/route */
    dst[16] = *(const volatile uint32_t *)0xF3000C68u; /* DEV0 W0/W1 en */
    dst[17] = *(const volatile uint32_t *)0xF3000CE0u; /* DEV0 cfg */
    dst[18] = *(const volatile uint32_t *)0xF3000D00u; /* DEV0 out */
    dst[19] = *(const volatile uint32_t *)0xF3000E00u;
    dst[20] = *(const volatile uint32_t *)0xF3010000u; /* RdcTop */
    dst[21] = *(const volatile uint32_t *)0xF3010004u; /* RdcTop trig */
    dst[22] = *(const volatile uint32_t *)0xF3010500u; /* RdcW0 cfg */
    dst[23] = *(const volatile uint32_t *)0xF3010A04u; /* Rdc sub */
}

static void snap_aif_bus(volatile uint32_t *dst) {
    dst[0]  = *(const volatile uint32_t *)0xF2804114u; /* tbl_188C[3] W0 bus/VMC ctrl */
    dst[1]  = *(const volatile uint32_t *)0xF2804118u; /* tbl_188C[4] W1 */
    dst[2]  = *(const volatile uint32_t *)0xF2804004u; /* tbl_188C[0] R0 */
    dst[3]  = *(const volatile uint32_t *)0xF2804008u; /* tbl_188C[1] R1 */
    dst[4]  = *(const volatile uint32_t *)0xF2807064u; /* tbl_6CC4[3] W0 bdmgr */
    dst[5]  = *(const volatile uint32_t *)0xF2807068u; /* tbl_6CC4[4] W1 */
    dst[6]  = *(const volatile uint32_t *)0xF2807004u; /* tbl_6CC4[0] R0 */
    dst[7]  = *(const volatile uint32_t *)0xF2802034u; /* ch3 0xF2802000+3*0x10+4 */
    dst[8]  = *(const volatile uint32_t *)0xF2802044u; /* ch4 W1 */
    dst[9]  = *(const volatile uint32_t *)0xF280411Cu; /* tbl_188C[5] W2 */
    dst[10] = *(const volatile uint32_t *)0xF2802004u; /* ch0 R0 */
    dst[11] = *(const volatile uint32_t *)0xF30023A8u; /* W0 +0x28 */
    dst[12] = *(const volatile uint32_t *)0xF30023ACu; /* W0 +0x2C */
    dst[13] = *(const volatile uint32_t *)0xF30023B0u; /* W0 +0x30 */
    dst[14] = *(const volatile uint32_t *)0xF30023B4u; /* W0 +0x34 */
    dst[15] = *(const volatile uint32_t *)0xF30023B8u; /* W0 +0x38 */
}

static void snap_rdcw0_hw(volatile uint32_t *dst) {
    dst[0]  = *(const volatile uint32_t *)0xF3010000u; /* RdcTop ctrl */
    dst[1]  = *(const volatile uint32_t *)0xF3010004u; /* RdcTop trig */
    dst[2]  = *(const volatile uint32_t *)0xF301000Cu; /* RdcW0 trig/en */
    dst[3]  = *(const volatile uint32_t *)0xF3010018u;
    /* 0x63D0DC40..0x63D0DD1A 实际写入的 RdcW0 寄存器位于 0xF3010700..0xF3010734 */
    dst[4]  = *(const volatile uint32_t *)0xF3010700u;
    dst[5]  = *(const volatile uint32_t *)0xF3010704u;
    dst[6]  = *(const volatile uint32_t *)0xF301070Cu;
    dst[7]  = *(const volatile uint32_t *)0xF3010714u;
    dst[8]  = *(const volatile uint32_t *)0xF3010720u;
    dst[9]  = *(const volatile uint32_t *)0xF3010730u;
    dst[10] = *(const volatile uint32_t *)0xF3010400u;
    dst[11] = *(const volatile uint32_t *)0xF3010404u;
    dst[12] = *(const volatile uint32_t *)0xF3010500u;
    dst[13] = *(const volatile uint32_t *)0xF3010504u;
    dst[14] = *(const volatile uint32_t *)0xF3010600u;
    dst[15] = *(const volatile uint32_t *)0xF3010A04u;
}

/* 记录 CamCore::CamAif::Enable (0x64587280) */
void zvp_spy_dfe_rdc_s10(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         uint32_t caller_lr) {
    (void)r1; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    s->dfe_rdc_calls++;
    s->dfe_rdc_enc = r0;
    if (s->rec_state != 0u) {
        d->rec_dfe_rdc_calls++;
        d->rec_last_camcore_this = r0;
    }
    if (r0 >= 0x60000000u && r0 < 0x80000000u) {
        uint32_t port = *(const volatile uint32_t *)(r0 + 0x20u);
        s->dfe_rdc_w = port;
        s->dfe_rdc_h = *(const volatile uint32_t *)(r0 + 0x28u);
        if (port < 8u) {
            d->camaif_port_hits[port]++;
            d->v18_aif_en_lr[port] = caller_lr;
            if (s->rec_state != 0u) {
                d->v18_aif_en_rec_lr[port] = caller_lr;
                if (port == 4u && d->v18_rec_camdev_regs[0] == 0u) {
                    snap_cam_ctrl(d->v18_rec_camdev_regs);
                }
            }
        }
        if (s->rec_state != 0u && port < 16u) {
            d->rec_port_hits[port]++;
            if (port < 6u) {
                snap_aif_5(d->core0_rec_regs[port], 0xF3002200u + (port << 7));
            } else if (port < 12u) {
                snap_aif_5(d->core1_rec_regs[port - 6u], 0xF3102200u + ((port - 6u) << 7));
            }
        }
    }
    if (r0 == 0x6413790Cu) {
        d->camaif0_en_calls++;
        snap_cam_ctrl(d->cam_ctrl_still);
        copy_mmio32(d->w0_at_en, 0xF3002380u, 32u);
        d->w0_at_en_valid = 1u;
    }
    mem_barrier();
}

/* 记录 CamCore::CamAif::Disable (0x64587284)：在传输完成后采集 R0/R1/W0/W1/W2 寄存器 */
void zvp_spy_dfe_rdc_cfg(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r1; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    s->dfe_rdc2_calls++;
    if (s->rec_state != 0u) {
        d->rec_dfe_rdc2_calls++;
        if (r0 >= 0x60000000u && r0 < 0x80000000u) {
            uint32_t port = *(const volatile uint32_t *)(r0 + 0x20u);
            if (port < 6u) {
                snap_aif_5(d->core0_rec_regs[port], 0xF3002200u + (port << 7));
            } else if (port < 12u) {
                snap_aif_5(d->core1_rec_regs[port - 6u], 0xF3102200u + ((port - 6u) << 7));
            }
        }
    }
    if (r0 == 0x6413790Cu) {
        d->camaif0_dis_calls++;
        copy_mmio32(d->w0_at_dis, 0xF3002380u, 16u);
        if (d->w0_at_dis[4] != 0u) {
            s->last_ring_pa_written = d->w0_at_dis[4]; /* +0x10 = cuAdrs */
        }
    } else if (r0 == 0x6413793Cu) { /* port 4: W1 */
        if (s->rec_state != 0u) {
            copy_mmio32(d->w1_at_rec, 0xF3002400u, 16u);
            d->w1_at_rec_valid = 1u;
        } else {
            copy_mmio32(d->w1_at_idle, 0xF3002400u, 16u);
        }
    } else if (r0 == 0x6413796Cu) { /* port 5: W2 */
        if (s->rec_state != 0u) {
            copy_mmio32(d->w2_at_rec, 0xF3002480u, 16u);
            d->w2_at_rec_valid = 1u;
        } else {
            copy_mmio32(d->w2_at_idle, 0xF3002480u, 16u);
        }
    } else if (r0 == 0x6413787Cu && s->rec_state != 0u) { /* port 0: R0 */
        copy_mmio32(d->r0_at_rec, 0xF3002200u, 16u);
    } else if (r0 == 0x641378AC && s->rec_state != 0u) { /* port 1: R1 */
        copy_mmio32(d->r1_at_rec, 0xF3002280u, 16u);
    }
    mem_barrier();
}

/* 记录 Sequence_Movie.cpp #2 (0x64639128) */
void zvp_spy_dfe_main(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r1; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->dfe_main_calls++;
    s->dfe_main_r0 = r0;
    uint32_t wp = *(const volatile uint32_t *)0x6420B580u;
    zvp_craw_dbg_t *d = DBG;
    if (wp != 0u) {
        ensure_dbg_init(d);
        d->lpcm_wp_val = wp;
    }
    if (s->rec_state != 0u) {
        ensure_dbg_init(d);
        d->rec_dfe_main_calls++;
        const volatile uint32_t *bm = (const volatile uint32_t *)0x642069ACu;
        const volatile uint32_t *aw = (const volatile uint32_t *)0x64206674u;
        const volatile uint32_t *ad = (const volatile uint32_t *)0x641FDD34u;
        for (uint32_t i = 0; i < 8u; ++i) {
            d->adf_buffmgr_rec[i] = bm[i];
            d->adf_write_rec[i] = aw[i];
        }
        for (uint32_t i = 0; i < 36u; ++i) {
            d->adf_buffmgr_full[i] = bm[i];
        }
        /* +0x88..+0xBC (14 words) + +0xE4 / +0xE8 */
        for (uint32_t i = 0; i < 14u; ++i) {
            d->adf_bm_tail_rec[i] = bm[0x22u + i];
        }
        d->adf_bm_tail_rec[14] = bm[0xE4u / 4u];
        d->adf_bm_tail_rec[15] = bm[0xE8u / 4u];
        /* 0x641FDD34 +0x30..+0x8C (24 words) */
        for (uint32_t i = 0; i < 24u; ++i) {
            d->adf_1fdd34_rec[i] = ad[0x0Cu + i];
        }
        uint32_t wp_cur = ad[0x44u / 4u];
        if (wp_cur != 0u) {
            s->audio_ptr = wp_cur;
            if (d->adf_wp_min == 0u || wp_cur < d->adf_wp_min) d->adf_wp_min = wp_cur;
            if (wp_cur > d->adf_wp_max) d->adf_wp_max = wp_cur;
            if (d->adf_buffmgr_nz_pa != wp_cur) {
                d->adf_buffmgr_nz_pa = wp_cur;
                d->adf_wp_changes++;
            }
        }
        d->adf_buffmgr_nz_sz = ad[0x48u / 4u];
        uint32_t v70 = ad[0x70u / 4u];
        if (d->adf_70_first == 0u && v70 != 0u) d->adf_70_first = v70;
        if (v70 != 0u) d->adf_70_last = v70;
        snap_cam_ctrl(d->cam_ctrl_rec);

        /* 通过 arr_mode[ch](0x645217D6) 确认驱动曾配置该通道后再调用 DMAC accessor 抽样巡检 */
        if ((d->rec_dfe_main_calls & 15u) == 1u) {
            for (uint32_t idx = 0u; idx < 12u; ++idx) {
                uint32_t ch = (idx < 3u) ? (3u + idx)
                            : (idx < 6u) ? (9u + (idx - 3u))
                            : (idx < 11u) ? (18u + (idx - 6u))
                            : 24u;
                volatile uint32_t *dst = (idx < 6u) ? d->core0_rec_regs[idx]
                                                    : d->core1_rec_regs[idx - 6u];
                uint8_t mode_val = *(const volatile uint8_t *)(0x645217D6u + ch);
                if (mode_val != 0xFFu) {
                    dst[0] = ((uint32_t (*)(uint32_t))0x639E2241u)(ch);
                    dst[1] = ((uint32_t (*)(uint32_t))0x639E22A9u)(ch);
                    dst[2] = ((uint32_t (*)(uint32_t))0x639E231Du)(ch);
                    dst[3] = ((uint32_t (*)(uint32_t))0x639E2361u)(ch);
                } else {
                    dst[0] = 0xDEAD00FFu;
                    dst[1] = *(const volatile uint32_t *)(0x643EB19Cu + ch * 4u);
                    dst[2] = 0u;
                    dst[3] = 0u;
                }
            }
        }
        if (s->mode == ZVP_CRAW_LIRO_ARM_2K) {
            w1_rec_service(s, d, 1u);
        } else if (s->mode == ZVP_CRAW_LIRO_RAW_W0) {
            w0_raw_rec_service(s, d, 0u);
        }
    }
    mem_barrier();
}

/* 记录 StateHandler (0x64575E28 -> 0x636ACEA5) 中的 ENC_PREPROC/POSTPROC 与 hsMovieRec 状态 */
void zvp_spy_state_hdl(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    s->state_calls++;
    s->state_last_this = r0;
    if (r1 >= 0x40000000u && r1 < 0x90000000u) {
        uint16_t msg_id = *(const volatile uint16_t *)(r1 + 2u);
        uint32_t hs = 0u, hs2 = 0u;
        if (msg_id == 0x0108u) {
            d->state_enc_pre_cnt++;
            if ((s->mode == ZVP_CRAW_LIRO_ARM_2K || s->mode == ZVP_CRAW_LIRO_RAW_W0) &&
                s->ring_base_pa != 0u && s->ring_total_bytes >= 0x00500000u) {
                const volatile uint32_t *ad = (const volatile uint32_t *)0x641FDD34u;
                if (s->mode == ZVP_CRAW_LIRO_ARM_2K) {
                    /* 按当前 W1 实际单帧尺寸 (3992*1122=4479024B -> 0x450000 64KB对齐) 划分多槽环形队列，不用除法指令 */
                    uint32_t slots = 0u, rem = s->ring_total_bytes;
                    while (rem >= 0x00450000u) {
                        slots++;
                        rem -= 0x00450000u;
                    }
                    s->frame_stride = 0x00450000u;
                    s->slot_count = (slots > 1u) ? slots : 2u;
                    s->target_w = 1996u;
                    s->target_h = 1122u;
                    s->target_enc_type = 0x59565955u; /* 'UYVY' */
                } else {
                    /* ZVP_CRAW_LIRO_RAW_W0: 维持已配好的 2.5 MiB 步长并初始化为 16-bit Bayer RAW 参数 */
                    if (s->frame_stride < 0x00200000u) {
                        s->frame_stride = 2560u * 1024u;
                    }
                    uint32_t slots = 0u, rem = s->ring_total_bytes;
                    while (rem >= s->frame_stride) {
                        slots++;
                        rem -= s->frame_stride;
                    }
                    s->slot_count = (slots > 1u) ? slots : 2u;
                    s->target_w = 1996u;
                    s->target_h = 570u;
                    s->target_enc_type = 1u; /* 16-bit linear uncompressed Bayer RAW */
                }
                s->write_seq = 0u;
                s->read_seq = 0u;
                s->drop_seq = 0u;
                s->last_ring_pa_written = 0u;
                s->audio_ptr = ad[0x44u / 4u];
                d->rec_w0_arm_cnt = 0u;
                d->rec_w0_done_cnt = 0u;
                d->rec_w0_err_cnt = 0u;
                d->rec_w0_cur_pa = 0u;
                d->dfe_ch_pa[3] = 0u;
                d->dfe_ch_pa[4] = 0u;
                d->dfe_ch_pa[5] = 0u;
                d->dfe_ch_pa[6] = 0u;
                d->dfe_ch_pa[7] = 0u;
                d->rec_w0_state = 1u;
                mem_barrier();
            }
            s->rec_state = 1u;
        } else if (msg_id == 0x0109u) {
            d->state_enc_post_cnt++;
            s->rec_state = 0u;
            if (d->dfe_ch_pa[3] == 0x5730u) {
                w0_raw_disarm(s, d);
            } else if (d->dfe_ch_pa[3] == 0x5731u) {
                volatile uint32_t *w1 = (volatile uint32_t *)0xF3002400u;
                uint32_t cur_c = w1[0x00u / 4u];
                w1[0x00u / 4u] = cur_c & ~1u;
                mem_barrier();
                for (uint32_t k = 0u; k < 65536u && (w1[0x04u / 4u] & 0x80000000u) != 0u; ++k) {
                }
                ((void (*)(uint32_t))0x639E2385u)(4u);
                w1[0x20u / 4u] = d->dfe_ch_pa[1];
                mem_barrier();
                w1[0x00u / 4u] = d->dfe_ch_pa[0];
                mem_barrier();
                d->dfe_ch_pa[3] = 0u;
            }
            d->rec_w0_state = 0u;
        } else if (msg_id == 0x0201u && *(const volatile uint32_t *)(r1 + 36u) == 528u) {
            uint32_t payload = *(const volatile uint32_t *)(r1 + 12u);
            if (payload >= 0x40000000u && payload < 0x90000000u) {
                hs  = *(const volatile uint8_t *)(payload + 0x52u);
                hs2 = *(const volatile uint8_t *)(payload + 0x53u);
                if (hs != 0u) {
                    s->rec_state = hs;
                    d->state_hs_nonzero_calls++;
                    if (hs > d->state_hs_max) d->state_hs_max = hs;
                    for (uint32_t i = 0; i < 23u; ++i) {
                        d->state_payload_rec[i] = ((const volatile uint32_t *)payload)[i];
                    }
                } else {
                    for (uint32_t i = 0; i < 23u; ++i) {
                        d->state_payload_idle[i] = ((const volatile uint32_t *)payload)[i];
                    }
                }
            }
        }
        uint32_t packed = ((hs2 & 0xFFu) << 24) | ((hs & 0xFFu) << 16) | (uint32_t)msg_id;
        s->state_last_msg_hs = packed;
        uint32_t idx = d->state_msg_idx++;
        d->state_msg_hist[idx & 15u] = packed;
    }
    mem_barrier();
}

/* 记录 DFECore::RdcW0::slot[10] (0x64588B58 -> 0x637A32BD) */
void zvp_spy_dfe_rdcw0(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r0; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    s->dfe_rdcw0_calls++;
    if (r1 >= 0x40000000u && r1 < 0x90000000u) {
        s->dfe_rdcw0_last_cfg = *(const volatile uint32_t *)r1;
    }
    mem_barrier();
}

#define ZVP_STR_1(x) #x
#define ZVP_STR(x)   ZVP_STR_1(x)

static int is_cam_ram_ptr(uint32_t p) {
    return (p >= 0x40000000u && p < 0x90000000u && (p & 3u) == 0u);
}

static void snap_ctrl_ports_if_needed(const zvp_craw_shm_t *s, zvp_craw_dbg_t *d,
                                      uint32_t ctrl_this) {
    if (!is_cam_ram_ptr(ctrl_this)) return;
    if (s->rec_state != 0u || d->v18_rec_ctrl_dump[0] == 0u) {
        const volatile uint32_t *p = (const volatile uint32_t *)(ctrl_this + 0x28u);
        for (uint32_t i = 0u; i < 16u; ++i) {
            d->v18_rec_ctrl_dump[i] = p[i];
        }
    }
}

/* v18 只读追踪: CamCore::RdcW1::slot[7] (0x645874D4 -> 0x6377600D) */
void zvp_spy_v18_rdcw1_s7(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          uint32_t caller_lr) {
    (void)r0; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_rdcw1_s7_calls++;
    d->v18_rdcw1_s7_lr = caller_lr;
    if (is_cam_ram_ptr(r1)) {
        const volatile uint32_t *cfg = (const volatile uint32_t *)r1;
        for (uint32_t i = 0u; i < 8u; ++i) {
            d->v18_rdcw1_s7_cfg[i] = cfg[i];
        }
    }
    mem_barrier();
}

/* v18 只读追踪: CamCore::RdcW1::slot[8] (0x645874D8 -> 0x63D0DC5D) */
void zvp_spy_v18_rdcw1_s8(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          uint32_t caller_lr) {
    (void)r0; (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_rdcw1_s8_calls++;
    d->v18_rdcw1_s8_lr = caller_lr;
    d->v18_rdcw1_s8_r1 = r1;
    mem_barrier();
}

/* v18 只读追踪: SplitEEController / EEController slot[2] (0x637730C5) */
void zvp_spy_v18_ctrl_s2(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         uint32_t caller_lr) {
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_ctrl_s2_calls++;
    d->v18_ctrl_s2_vptr = is_cam_ram_ptr(r0) ? *(const volatile uint32_t *)r0 : 0u;
    d->v18_ctrl_s2_lr = caller_lr;
    d->v18_ctrl_s2_r1 = r1;
    d->v18_ctrl_s2_r2 = r2;
    d->v18_ctrl_s2_r3 = r3;
    snap_ctrl_ports_if_needed(s, d, r0);
    mem_barrier();
}

/* v18 只读追踪: SplitEEController / EEController slot[17] (0x637721ED) */
void zvp_spy_v18_ctrl_s17(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          uint32_t caller_lr) {
    (void)r2; (void)r3;
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_ctrl_s17_calls++;
    d->v18_ctrl_s17_vptr = is_cam_ram_ptr(r0) ? *(const volatile uint32_t *)r0 : 0u;
    d->v18_ctrl_s17_lr = caller_lr;
    d->v18_ctrl_s17_r1 = r1;
    if (is_cam_ram_ptr(r1)) {
        const volatile uint32_t *w = (const volatile uint32_t *)r1;
        d->v18_ctrl_s17_w[0] = w[0x00u / 4u];
        d->v18_ctrl_s17_w[1] = w[0x04u / 4u];
        d->v18_ctrl_s17_w[2] = w[0x60u / 4u];
        d->v18_ctrl_s17_w[3] = w[0x68u / 4u];
        d->v18_ctrl_s17_w[4] = w[0x6Cu / 4u];
        d->v18_ctrl_s17_w[5] = w[0x70u / 4u];
        d->v18_ctrl_s17_w[6] = w[0x74u / 4u];
        d->v18_ctrl_s17_w[7] = w[0x78u / 4u];
    }
    snap_ctrl_ports_if_needed(s, d, r0);
    mem_barrier();
}

/* v18 只读追踪: SplitEEController::slot[22] (0x63774A69) */
void zvp_spy_v18_ctrl_s22(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          uint32_t caller_lr) {
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_ctrl_s22_calls++;
    d->v18_ctrl_s22_lr = caller_lr;
    d->v18_ctrl_s22_r1 = r1;
    d->v18_ctrl_s22_r2 = r2;
    d->v18_ctrl_s22_r3 = r3;
    snap_ctrl_ports_if_needed(s, d, r0);
    mem_barrier();
}

/* v18 只读追踪: SplitCapController::slot[23] (0x63773D59) */
void zvp_spy_v18_ctrl_s23(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          uint32_t caller_lr) {
    zvp_craw_shm_t *s = SHM;
    if (s->magic != ZVP_CRAW_SHM_MAGIC || s->mode == ZVP_CRAW_LIRO_BYPASS) return;
    zvp_craw_dbg_t *d = DBG;
    ensure_dbg_init(d);
    d->v18_ctrl_s23_calls++;
    d->v18_ctrl_s23_lr = caller_lr;
    d->v18_ctrl_s23_r1 = r1;
    d->v18_ctrl_s23_r2 = r2;
    d->v18_ctrl_s23_r3 = r3;
    snap_ctrl_ports_if_needed(s, d, r0);
    mem_barrier();
}

/* 保留全部寄存器与调用者栈参数的 Thumb-2 裸蹦床（尾跳至原厂函数） */
#define DEFINE_NAKED_SPY_TRAMPOLINE(fn_name, helper_fn, orig_va) \
    __attribute__((naked)) void fn_name(void) {                  \
        __asm__ volatile (                                       \
            ".syntax unified\n"                                  \
            "push {r0, r1, r2, r3, r12, lr}\n"                   \
            "bl " #helper_fn "\n"                                \
            "pop  {r0, r1, r2, r3, r12, lr}\n"                   \
            "ldr  r12, 1f\n"                                     \
            "bx   r12\n"                                         \
            ".balign 4\n"                                        \
            "1: .word " ZVP_STR(orig_va) "\n"                    \
        );                                                       \
    }

#define DEFINE_NAKED_SPY_TRAMPOLINE_LR(fn_name, helper_fn, orig_va) \
    __attribute__((naked)) void fn_name(void) {                     \
        __asm__ volatile (                                          \
            ".syntax unified\n"                                     \
            "push {r0, r1, r2, r3, r12, lr}\n"                      \
            "sub  sp, sp, #8\n"                                     \
            "str  lr, [sp, #0]\n"                                   \
            "bl " #helper_fn "\n"                                   \
            "add  sp, sp, #8\n"                                     \
            "pop  {r0, r1, r2, r3, r12, lr}\n"                      \
            "ldr  r12, 1f\n"                                        \
            "bx   r12\n"                                            \
            ".balign 4\n"                                           \
            "1: .word " ZVP_STR(orig_va) "\n"                       \
        );                                                          \
    }

DEFINE_NAKED_SPY_TRAMPOLINE(hook_seq_movie,  zvp_spy_seq_movie,  ZVP_AVCAM_ORIG_SEQ_MOVIE)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_camrdc_cfg, zvp_spy_camrdc_cfg, ZVP_AVCAM_ORIG_CAMRDC_CFG)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_camaif_s4,  zvp_spy_camaif_s4,  ZVP_AVCAM_ORIG_CAMAIF_S4)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_ifmt_s4,    zvp_spy_ifmt_s4,    ZVP_AVCAM_ORIG_IFMT_S4)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_ifmt_s10,   zvp_spy_ifmt_s10,   ZVP_AVCAM_ORIG_IFMT_S10)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_dfe_rdc_s10, zvp_spy_dfe_rdc_s10, ZVP_AVCAM_ORIG_DFE_RDC_S10)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_dfe_rdc_cfg, zvp_spy_dfe_rdc_cfg, ZVP_AVCAM_ORIG_DFE_RDC_CFG)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_dfe_main,    zvp_spy_dfe_main,    ZVP_AVCAM_ORIG_DFE_MAIN)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_state_hdl,   zvp_spy_state_hdl,   ZVP_AVCAM_ORIG_STATE_HDL)
DEFINE_NAKED_SPY_TRAMPOLINE(hook_dfe_rdcw0,   zvp_spy_dfe_rdcw0,   ZVP_AVCAM_ORIG_DFE_RDCW0)

DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_rdcw1_s7,    zvp_spy_v18_rdcw1_s7, ZVP_AVCAM_EXT_ORIG_RDCW1_S7)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_rdcw1_s8,    zvp_spy_v18_rdcw1_s8, ZVP_AVCAM_EXT_ORIG_RDCW1_S8)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_spl_ee_s2,   zvp_spy_v18_ctrl_s2,  ZVP_AVCAM_EXT_ORIG_SPL_EE_S2)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_ee_s2,       zvp_spy_v18_ctrl_s2,  ZVP_AVCAM_EXT_ORIG_EE_S2)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_spl_ee_s17,  zvp_spy_v18_ctrl_s17, ZVP_AVCAM_EXT_ORIG_SPL_EE_S17)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_ee_s17,      zvp_spy_v18_ctrl_s17, ZVP_AVCAM_EXT_ORIG_EE_S17)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_spl_ee_s22,  zvp_spy_v18_ctrl_s22, ZVP_AVCAM_EXT_ORIG_SPL_EE_S22)
DEFINE_NAKED_SPY_TRAMPOLINE_LR(hook_ext_spl_cap_s23, zvp_spy_v18_ctrl_s23, ZVP_AVCAM_EXT_ORIG_SPL_CAP_S23)

/* 固定位于 0x6471AD00 的派发头，供用户态动态解析每个入口地址 */
__attribute__((section(".text.zvp_liro_header"), used))
const zvp_liro_dispatch_header_t zvp_liro_header = {
    ZVP_CRAW_SHM_MAGIC,
    ZVP_CRAW_SHM_VERSION,
    {
        (uint32_t)hook_seq_movie,
        (uint32_t)hook_rdcw0_s7,
        (uint32_t)hook_camrdc_cfg,
        (uint32_t)hook_camaif_s4,
        (uint32_t)hook_ifmt_s4,
        (uint32_t)hook_ifmt_s10,
        (uint32_t)hook_dfe_rdc_s10,
        (uint32_t)hook_dfe_rdc_cfg,
        (uint32_t)hook_dfe_main,
        (uint32_t)hook_state_hdl,
        (uint32_t)hook_dfe_rdcw0,
    },
    {
        (uint32_t)hook_ext_rdcw1_s7,
        (uint32_t)hook_ext_rdcw1_s8,
        (uint32_t)hook_ext_spl_ee_s2,
        (uint32_t)hook_ext_ee_s2,
        (uint32_t)hook_ext_spl_ee_s17,
        (uint32_t)hook_ext_ee_s17,
        (uint32_t)hook_ext_spl_ee_s22,
        (uint32_t)hook_ext_spl_cap_s23,
    }
};
