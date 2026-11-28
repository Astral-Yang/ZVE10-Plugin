#include "zvp_lut.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(zvp_lut_adjust_t) == 6, "zvp_lut_adjust_t must be 6 bytes");
_Static_assert(sizeof(zvp_lut_slot_packet_t) == 2048, "zvp_lut_slot_packet_t must be 2048 bytes");
_Static_assert(offsetof(zvp_lut_slot_packet_t, coef) == 26, "slot coef offset mismatch");
_Static_assert(offsetof(zvp_lut_slot_packet_t, gamma96) == 44, "slot gamma96 offset mismatch");
_Static_assert(offsetof(zvp_lut_slot_packet_t, grid1620) == 428, "slot grid1620 offset mismatch");
_Static_assert(sizeof(zvp_lut_cfg_look_t) == 38, "zvp_lut_cfg_look_t must be 38 bytes");
_Static_assert(offsetof(zvp_lut_cfg_disk_t, sharpness) == ZVP_LUT_CFG_V4_CRC_OFF, "v4 prefix offset mismatch");
_Static_assert(offsetof(zvp_lut_cfg_disk_t, crc32) == 744, "cfg crc32 offset mismatch");
_Static_assert(sizeof(zvp_lut_cfg_disk_t) == 748, "zvp_lut_cfg_disk_t must be 748 bytes");
#endif

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool id_equal(const char *a, const char *b)
{
    for (size_t i = 0u; i < ZVP_LUT_ID_BYTES; i++) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
    return false;
}

static int validate_utf8_cstr(const uint8_t *src, size_t cap, char *out_name, size_t out_cap)
{
    size_t i = 0u;
    size_t used = 0u;
    while (i < cap && src[i] != 0u) {
        uint8_t first = src[i];
        size_t n = (first >= 0x20u && first < 0x7fu) ? 1u :
                   (first >= 0xc2u && first <= 0xdfu) ? 2u :
                   (first >= 0xe0u && first <= 0xefu) ? 3u :
                   (first >= 0xf0u && first <= 0xf4u) ? 4u : 0u;
        if (n == 0u || i + n >= cap) {
            return -EINVAL;
        }
        for (size_t j = 1u; j < n; j++) {
            if ((src[i + j] & 0xc0u) != 0x80u) {
                return -EINVAL;
            }
        }
        if ((first == 0xe0u && src[i + 1] < 0xa0u) ||
            (first == 0xedu && src[i + 1] >= 0xa0u) ||
            (first == 0xf0u && src[i + 1] < 0x90u) ||
            (first == 0xf4u && src[i + 1] >= 0x90u)) {
            return -EINVAL;
        }
        if (out_name && out_cap > 1u && i + n < out_cap) {
            memcpy(out_name + i, src + i, n);
            used = i + n;
        }
        i += n;
    }
    if (i == 0u || i == cap) {
        return -EINVAL;
    }
    if (out_name && out_cap > 0u) {
        out_name[used] = '\0';
    }
    for (; i < cap; i++) {
        if (src[i] != 0u) {
            return -EINVAL;
        }
    }
    return 0;
}

uint32_t zvp_lut_crc32(const void *data, size_t len)
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

bool zvp_lut_adjust_valid(zvp_lut_adjust_t a)
{
    return a.contrast >= -9 && a.contrast <= 9 &&
           a.saturation >= -9 && a.saturation <= 9 &&
           a.highlights >= -9 && a.highlights <= 9 &&
           a.shadows >= -9 && a.shadows <= 9 &&
           a.fade <= 9u && a.strength10 >= 1u && a.strength10 <= 10u;
}

bool zvp_lut_sharpness_valid(int value)
{
    return value >= ZVP_LUT_SHARPNESS_MIN && value <= ZVP_LUT_SHARPNESS_MAX;
}

uint32_t zvp_lut_pack_adjust(zvp_lut_adjust_t a)
{
    return (uint32_t)(a.contrast + 9) |
           ((uint32_t)(a.saturation + 9) << 5) |
           ((uint32_t)(a.highlights + 9) << 10) |
           ((uint32_t)(a.shadows + 9) << 15) |
           ((uint32_t)a.fade << 20) |
           ((uint32_t)a.strength10 << 24);
}

zvp_lut_adjust_t zvp_lut_unpack_adjust(uint32_t bits)
{
    zvp_lut_adjust_t a;
    a.contrast   = (int8_t)((int)(bits & 31u) - 9);
    a.saturation = (int8_t)((int)((bits >> 5) & 31u) - 9);
    a.highlights = (int8_t)((int)((bits >> 10) & 31u) - 9);
    a.shadows    = (int8_t)((int)((bits >> 15) & 31u) - 9);
    a.fade       = (uint8_t)((bits >> 20) & 15u);
    a.strength10 = (uint8_t)((bits >> 24) & 15u);
    return a;
}

static void decode_slot_packet(const uint8_t *e, uint32_t slot, zvp_lut_slot_packet_t *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->slot_idx = slot;
    pkt->grid_enabled = rd_le32(e + 100);
    pkt->pad0 = 0u;
    memcpy(pkt->matrix, e + 112, sizeof(pkt->matrix));
    memcpy(pkt->coef, e + 132, sizeof(pkt->coef));
    memcpy(pkt->gamma96, e + 148, sizeof(pkt->gamma96));
    memcpy(pkt->grid1620, e + 532, sizeof(pkt->grid1620));
}

int zvp_lut_validate_bundle(const uint8_t *bytes, size_t size, uint32_t *out_count, uint32_t *out_digest)
{
    if (!bytes || size < (size_t)(ZVP_LUT_BUNDLE_HEADER + 4u) || size > ZVP_LUT_BUNDLE_MAX_BYTES) {
        return -EINVAL;
    }
    if (memcmp(bytes, ZVP_LUT_BUNDLE_MAGIC, 8u) != 0) {
        return -EINVAL;
    }
    uint32_t ver = rd_le32(bytes + 8);
    uint32_t count = rd_le32(bytes + 12);
    uint32_t entry_sz = rd_le32(bytes + 16);
    if (ver != 1u || count == 0u || count > ZVP_LUT_VISIBLE_LOOKS ||
        entry_sz != ZVP_LUT_BUNDLE_ENTRY ||
        size != (size_t)ZVP_LUT_BUNDLE_HEADER + (size_t)count * ZVP_LUT_BUNDLE_ENTRY + 4u) {
        return -EINVAL;
    }
    uint32_t digest = rd_le32(bytes + size - 4u);
    if (zvp_lut_crc32(bytes, size - 4u) != digest) {
        return -EINVAL;
    }

    for (uint32_t i = 0u; i < count; i++) {
        const uint8_t *e = bytes + ZVP_LUT_BUNDLE_HEADER + i * ZVP_LUT_BUNDLE_ENTRY;
        if (zvp_lut_crc32(e, ZVP_LUT_BUNDLE_ENTRY - 4u) != rd_le32(e + ZVP_LUT_BUNDLE_ENTRY - 4u)) {
            return -EINVAL;
        }
        if (validate_utf8_cstr(e, ZVP_LUT_ID_BYTES, NULL, 0u) != 0 ||
            validate_utf8_cstr(e + 32, ZVP_LUT_NAME_BYTES, NULL, 0u) != 0) {
            return -EINVAL;
        }
        if (rd_le32(e + 96) == 0u || rd_le32(e + 100) > 1u ||
            e[110] != 0u || e[111] != 0u || e[130] != 0u || e[131] != 0u) {
            return -EINVAL;
        }
        for (uint32_t j = 0u; j < i; j++) {
            const uint8_t *ej = bytes + ZVP_LUT_BUNDLE_HEADER + j * ZVP_LUT_BUNDLE_ENTRY;
            if (id_equal((const char *)ej, (const char *)e)) {
                return -EINVAL;
            }
        }

        zvp_lut_adjust_t a;
        a.contrast   = (int8_t)e[104];
        a.highlights = (int8_t)e[105];
        a.shadows    = (int8_t)e[106];
        a.saturation = (int8_t)e[107];
        a.fade       = e[108];
        a.strength10 = e[109];
        if (!zvp_lut_adjust_valid(a)) {
            return -EINVAL;
        }

        zvp_lut_slot_packet_t pkt;
        decode_slot_packet(e, i, &pkt);
        for (uint32_t j = 0u; j < 9u; j++) {
            if (pkt.matrix[j] < -4096 || pkt.matrix[j] > 4096) {
                return -EINVAL;
            }
        }
        for (uint32_t j = 0u; j < 8u; j++) {
            if (pkt.coef[j] < (j < 4u ? -256 : 0) || pkt.coef[j] > 255) {
                return -EINVAL;
            }
        }
        for (uint32_t j = 0u; j < 96u; j++) {
            if (pkt.gamma96[j] > 1023u || (j > 0u && pkt.gamma96[j] < pkt.gamma96[j - 1u])) {
                return -EINVAL;
            }
        }
        for (uint32_t j = 0u; j < 405u; j++) {
            if ((pkt.grid1620[j] & ~0x007f7f7fu) != 0u || (!pkt.grid_enabled && pkt.grid1620[j] != 0u)) {
                return -EINVAL;
            }
        }
    }

    if (out_count) *out_count = count;
    if (out_digest) *out_digest = digest;
    return 0;
}

static int parse_bundle_into_ctx(zvp_lut_ctx_t *ctx, const uint8_t *bytes, size_t size)
{
    uint32_t count = 0u, digest = 0u;
    if (!ctx) {
        return -EINVAL;
    }
    int vrc = zvp_lut_validate_bundle(bytes, size, &count, &digest);
    if (vrc != 0) {
        return vrc;
    }

    memset(ctx->look_ids, 0, sizeof(ctx->look_ids));
    memset(ctx->look_names, 0, sizeof(ctx->look_names));
    memset(ctx->default_adjust, 0, sizeof(ctx->default_adjust));
    memset(ctx->adjust, 0, sizeof(ctx->adjust));
    for (uint32_t i = 0u; i < ZVP_LUT_NUM_SLOTS; i++) {
        ctx->sharpness[i] = ZVP_LUT_SHARPNESS_NATIVE;
        ctx->default_adjust[i].strength10 = 10u;
        ctx->adjust[i].strength10 = 10u;
    }
    snprintf(ctx->look_names[0], sizeof(ctx->look_names[0]), "%s", "关闭 LUT (原厂)");

    for (uint32_t i = 1u; i <= count; i++) {
        const uint8_t *e = bytes + ZVP_LUT_BUNDLE_HEADER + (i - 1u) * ZVP_LUT_BUNDLE_ENTRY;
        memcpy(ctx->look_ids[i], e, ZVP_LUT_ID_BYTES);
        (void)validate_utf8_cstr(e + 32, ZVP_LUT_NAME_BYTES, ctx->look_names[i], sizeof(ctx->look_names[i]));

        zvp_lut_adjust_t a;
        a.contrast   = (int8_t)e[104];
        a.highlights = (int8_t)e[105];
        a.shadows    = (int8_t)e[106];
        a.saturation = (int8_t)e[107];
        a.fade       = e[108];
        a.strength10 = e[109];
        ctx->default_adjust[i] = a;
        ctx->adjust[i] = a;
    }

    if (bytes != ctx->bundle_bytes) {
        memcpy(ctx->bundle_bytes, bytes, size);
    }
    ctx->bundle_digest = digest;
    ctx->bundle_size = (uint32_t)size;
    ctx->num_slots = count + 1u;
    ctx->bundle_ready = true;
    return 0;
}

static bool cfg_v5_valid(const zvp_lut_cfg_disk_t *cfg)
{
    if (!cfg || cfg->magic != ZVP_LUT_CFG_MAGIC || cfg->version != ZVP_LUT_CFG_VERSION) {
        return false;
    }
    return zvp_lut_crc32(cfg, sizeof(*cfg) - sizeof(cfg->crc32)) == cfg->crc32;
}

static void load_cfg_from_disk(zvp_lut_ctx_t *ctx)
{
    ctx->active_sel = 0u;
    if (!ctx || !ctx->ops.read_file || !ctx->bundle_ready) {
        return;
    }

    zvp_lut_cfg_disk_t cfg;
    size_t got = 0u;
    memset(&cfg, 0, sizeof(cfg));
    if (ctx->ops.read_file(ZVP_LUT_CFG_PATH, &cfg, sizeof(cfg), &got) != 0) {
        return;
    }

    bool v5 = (got == sizeof(cfg));
    if (v5) {
        if (!cfg_v5_valid(&cfg)) {
            return;
        }
    } else {
        if (got != (size_t)(ZVP_LUT_CFG_V4_CRC_OFF + 4u) ||
            cfg.magic != ZVP_LUT_CFG_MAGIC || cfg.version != 4u) {
            return;
        }
        const uint8_t *crc_p = (const uint8_t *)&cfg + ZVP_LUT_CFG_V4_CRC_OFF;
        if (rd_le32(crc_p) != zvp_lut_crc32(&cfg, ZVP_LUT_CFG_V4_CRC_OFF)) {
            return;
        }
    }

    for (uint32_t i = 1u; i < ctx->num_slots; i++) {
        const char *id = ctx->look_ids[i];
        if (id_equal(cfg.selected_id, id)) {
            ctx->active_sel = i;
        }
        for (uint32_t j = 0u; j < ZVP_LUT_VISIBLE_LOOKS; j++) {
            const zvp_lut_cfg_look_t *entry = &cfg.looks[j];
            if (id_equal(entry->id, id) && zvp_lut_adjust_valid(entry->adjust)) {
                ctx->adjust[i] = entry->adjust;
                if (v5 && zvp_lut_sharpness_valid(cfg.sharpness[j])) {
                    ctx->sharpness[i] = cfg.sharpness[j];
                }
                break;
            }
        }
    }
}

static int save_cfg_to_disk(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->ops.atomic_write_file || !ctx->bundle_ready) {
        return -ENOSYS;
    }

    zvp_lut_cfg_disk_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = ZVP_LUT_CFG_MAGIC;
    cfg.version = ZVP_LUT_CFG_VERSION;
    if (ctx->active_sel > 0u && ctx->active_sel < ctx->num_slots) {
        memcpy(cfg.selected_id, ctx->look_ids[ctx->active_sel], ZVP_LUT_ID_BYTES);
    }
    uint32_t count = (ctx->num_slots > 1u) ? (ctx->num_slots - 1u) : 0u;
    if (count > ZVP_LUT_VISIBLE_LOOKS) {
        count = ZVP_LUT_VISIBLE_LOOKS;
    }
    for (uint32_t j = 0u; j < count; j++) {
        uint32_t slot = j + 1u;
        memcpy(cfg.looks[j].id, ctx->look_ids[slot], ZVP_LUT_ID_BYTES);
        cfg.looks[j].adjust = ctx->adjust[slot];
        cfg.sharpness[j] = ctx->sharpness[slot];
    }
    cfg.crc32 = zvp_lut_crc32(&cfg, sizeof(cfg) - sizeof(cfg.crc32));

    int rc = ctx->ops.atomic_write_file(ZVP_LUT_DIR_PATH, ZVP_LUT_CFG_PATH, &cfg, sizeof(cfg));
    ctx->last_cfg_rc = rc;
    if (rc == 0) {
        ctx->cfg_dirty = false;
    }
    return rc;
}

static long catalog_cmd(const zvp_lut_ctx_t *ctx, uint32_t action, uintptr_t arg)
{
    return ctx->ops.sys_write_fd(ZVP_LUT_FD_CATALOG, arg, (uintptr_t)action);
}

static int apply_kernel_selection(zvp_lut_ctx_t *ctx, uint32_t sel)
{
    if (!ctx || !ctx->ops.sys_write_fd || !ctx->bundle_ready || sel >= ctx->num_slots) {
        return -EINVAL;
    }
    long rc = -EINVAL;
    for (int attempt = 0; attempt < 3; attempt++) {
        rc = ctx->ops.sys_write_fd(ZVP_LUT_FD_APPLY,
                                   (uintptr_t)zvp_lut_pack_adjust(ctx->adjust[sel]),
                                   (uintptr_t)sel);
        if (rc != -EBUSY) {
            break;
        }
    }
    ctx->last_apply_rc = (int)rc;
    return (int)rc;
}

static void refresh_preview_if_needed(zvp_lut_ctx_t *ctx, uint32_t sel)
{
    if (!ctx || !ctx->ops.refresh_preview || sel >= ctx->num_slots) {
        return;
    }
    zvp_lut_preview_info_t info;
    memset(&info, 0, sizeof(info));
    int rc = ctx->ops.refresh_preview(ctx->sharpness[sel], &ctx->native_sharpness, &info);
    if (info.rc == 0 && rc != 0) {
        info.rc = rc;
    }
    ctx->last_preview = info;
}

void zvp_lut_init(zvp_lut_ctx_t *ctx, const zvp_lut_platform_ops_t *ops)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (ops) {
        ctx->ops = *ops;
    }
    ctx->native_sharpness = ZVP_LUT_SHARPNESS_NATIVE;
    ctx->num_slots = 1u;
    snprintf(ctx->look_names[0], sizeof(ctx->look_names[0]), "%s", "关闭 LUT (原厂)");
    for (uint32_t i = 0u; i < ZVP_LUT_NUM_SLOTS; i++) {
        ctx->sharpness[i] = ZVP_LUT_SHARPNESS_NATIVE;
        ctx->default_adjust[i].strength10 = 10u;
        ctx->adjust[i].strength10 = 10u;
    }
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "%s", "待机 · 按需加载 zvc_fl4.ko");
    ctx->initialized = true;
}

int zvp_lut_load_bundle_and_cfg(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->initialized) {
        return -EINVAL;
    }

    bool disk_ok = false;
    size_t got = 0u;
    if (ctx->ops.read_file &&
        ctx->ops.read_file(ZVP_LUT_BUNDLE_PATH, ctx->bundle_bytes, sizeof(ctx->bundle_bytes), &got) == 0) {
        if (parse_bundle_into_ctx(ctx, ctx->bundle_bytes, got) == 0) {
            disk_ok = true;
            ctx->bundle_from_embedded = false;
        }
    }

    if (!disk_ok) {
        if (!ctx->ops.default_bundle_data || ctx->ops.default_bundle_len == 0u) {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "%s", "未找到有效 ZVLOOK01 滤镜包");
            return -ENOENT;
        }
        int prc = parse_bundle_into_ctx(ctx,
                                        (const uint8_t *)ctx->ops.default_bundle_data,
                                        ctx->ops.default_bundle_len);
        if (prc != 0) {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "内嵌滤镜包校验失败 rc=%d", prc);
            return prc;
        }
        ctx->bundle_from_embedded = true;
        if (ctx->ops.atomic_write_file) {
            (void)ctx->ops.atomic_write_file(ZVP_LUT_DIR_PATH, ZVP_LUT_BUNDLE_PATH,
                                             ctx->bundle_bytes, ctx->bundle_size);
        }
    }

    load_cfg_from_disk(ctx);
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "就绪 (%u款%s) · 当前: %s",
             (unsigned)(ctx->num_slots > 0u ? ctx->num_slots - 1u : 0u),
             ctx->bundle_from_embedded ? "/内置" : "/闪存",
             ctx->look_names[ctx->active_sel]);
    return 0;
}

static long upload_packets_to_kernel(const zvp_lut_ctx_t *ctx, const uint8_t *bytes, uint32_t count)
{
    long rc = 0;
    for (uint32_t i = 0u; rc == 0 && i < count; i++) {
        const uint8_t *e = bytes + ZVP_LUT_BUNDLE_HEADER + i * ZVP_LUT_BUNDLE_ENTRY;
        zvp_lut_slot_packet_t pkt;
        decode_slot_packet(e, i, &pkt);
        rc = ctx->ops.sys_write_fd(ZVP_LUT_FD_LOAD, (uintptr_t)&pkt, sizeof(pkt));
    }
    return rc;
}

int zvp_lut_ensure_kernel_ready(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->initialized) {
        return -EINVAL;
    }
    if (!ctx->bundle_ready) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    if (!ctx->ops.init_module || !ctx->ops.sys_write_fd) {
        return -ENOSYS;
    }

    if (!ctx->kmod_loaded) {
        if (!ctx->ops.default_ko_data || ctx->ops.default_ko_len == 0u) {
            return -ENOENT;
        }
        int krc = ctx->ops.init_module(ctx->ops.default_ko_data, ctx->ops.default_ko_len, "");
        ctx->last_kmod_rc = krc;
        if (krc != 0 && krc != -EEXIST) {
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "加载 zvc_fl4.ko 失败 rc=%d", krc);
            return krc;
        }
        ctx->kmod_loaded = true;
    }

    if (!ctx->catalog_uploaded) {
        long mrc = catalog_cmd(ctx, ZVP_LUT_CAT_MATCH, (uintptr_t)ctx->bundle_digest);
        if (mrc == 0) {
            ctx->last_catalog_rc = 0;
            ctx->catalog_uploaded = true;
            return 0;
        }

        uint32_t count = (ctx->num_slots > 1u) ? (ctx->num_slots - 1u) : 0u;
        long rc = catalog_cmd(ctx, count, 0u);
        if (rc == 0) {
            rc = upload_packets_to_kernel(ctx, ctx->bundle_bytes, count);
        }
        if (rc == 0) {
            rc = catalog_cmd(ctx, ZVP_LUT_CAT_COMMIT, (uintptr_t)ctx->bundle_digest);
        }
        if (rc != 0) {
            (void)catalog_cmd(ctx, ZVP_LUT_CAT_ABORT, 0u);
            ctx->last_catalog_rc = (int)rc;
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "上载LUT目录失败 rc=%d", (int)rc);
            return (int)rc;
        }
        ctx->last_catalog_rc = 0;
        ctx->catalog_uploaded = true;
    }

    return 0;
}

int zvp_lut_boot_activate(zvp_lut_ctx_t *ctx)
{
    int rc = zvp_lut_load_bundle_and_cfg(ctx);
    if (rc != 0) {
        return rc;
    }
    if (ctx->active_sel == 0u) {
        /* 处于关闭 LUT 状态时保持 0 内核钩子加载，延迟至用户首次启用时加载 */
        return 0;
    }
    return zvp_lut_select_slot(ctx, ctx->active_sel, false, true);
}

int zvp_lut_select_slot(zvp_lut_ctx_t *ctx, uint32_t sel, bool persist_cfg, bool refresh_ui_preview)
{
    if (!ctx || !ctx->initialized) {
        return -EINVAL;
    }
    if (!ctx->bundle_ready) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    if (sel >= ctx->num_slots) {
        return -EINVAL;
    }

    if (sel == 0u && !ctx->kmod_loaded) {
        ctx->active_sel = 0u;
        ctx->last_apply_rc = 0;
        if (persist_cfg) {
            (void)save_cfg_to_disk(ctx);
        } else {
            ctx->cfg_dirty = true;
        }
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "%s", "已关闭 LUT · 保持原厂成像");
        return 0;
    }

    int krc = zvp_lut_ensure_kernel_ready(ctx);
    if (krc != 0) {
        return krc;
    }

    int arc = apply_kernel_selection(ctx, sel);
    if (arc != 0) {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "应用LUT[%u]失败 rc=%d", (unsigned)sel, arc);
        return arc;
    }

    ctx->active_sel = sel;
    if (refresh_ui_preview) {
        refresh_preview_if_needed(ctx, sel);
    }
    if (persist_cfg) {
        (void)save_cfg_to_disk(ctx);
    } else {
        ctx->cfg_dirty = true;
    }

    if (sel == 0u) {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已关闭 LUT (preview rc=%d)", ctx->last_preview.rc);
    } else {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已启用 [%u/%u] %s",
                 (unsigned)sel, (unsigned)(ctx->num_slots - 1u), ctx->look_names[sel]);
    }
    return 0;
}

int zvp_lut_step_slot(zvp_lut_ctx_t *ctx, int delta, bool persist_cfg)
{
    if (!ctx || !ctx->bundle_ready || ctx->num_slots == 0u) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    int32_t n = (int32_t)ctx->num_slots;
    int32_t next = ((int32_t)ctx->active_sel + delta) % n;
    if (next < 0) {
        next += n;
    }
    return zvp_lut_select_slot(ctx, (uint32_t)next, persist_cfg, true);
}

int zvp_lut_toggle_enable(zvp_lut_ctx_t *ctx, uint32_t *io_last_nonzero_sel, bool persist_cfg)
{
    if (!ctx) {
        return -EINVAL;
    }
    if (!ctx->bundle_ready) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    if (ctx->active_sel > 0u) {
        if (io_last_nonzero_sel) {
            *io_last_nonzero_sel = ctx->active_sel;
        }
        return zvp_lut_select_slot(ctx, 0u, persist_cfg, true);
    }
    uint32_t target = (io_last_nonzero_sel && *io_last_nonzero_sel > 0u && *io_last_nonzero_sel < ctx->num_slots)
                          ? *io_last_nonzero_sel
                          : ((ctx->num_slots > 1u) ? 1u : 0u);
    return zvp_lut_select_slot(ctx, target, persist_cfg, true);
}

int zvp_lut_force_refresh(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->initialized) {
        return -EINVAL;
    }
    if (!ctx->bundle_ready) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    if (ctx->active_sel > 0u) {
        int rc = zvp_lut_ensure_kernel_ready(ctx);
        if (rc != 0) {
            return rc;
        }
        rc = apply_kernel_selection(ctx, ctx->active_sel);
        if (rc != 0) {
            return rc;
        }
    } else if (ctx->kmod_loaded) {
        (void)apply_kernel_selection(ctx, 0u);
    }
    refresh_preview_if_needed(ctx, ctx->active_sel);
    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "刷新预览完成 rc=%d (style=%d)",
             ctx->last_preview.rc, ctx->last_preview.style);
    return ctx->last_preview.rc;
}

int zvp_lut_reset_current_adjust(zvp_lut_ctx_t *ctx, bool persist_cfg)
{
    if (!ctx || !ctx->bundle_ready || ctx->active_sel >= ctx->num_slots) {
        return -EINVAL;
    }
    uint32_t sel = ctx->active_sel;
    ctx->adjust[sel] = ctx->default_adjust[sel];
    ctx->sharpness[sel] = ZVP_LUT_SHARPNESS_NATIVE;
    if (sel == 0u && !ctx->kmod_loaded) {
        return 0;
    }
    return zvp_lut_select_slot(ctx, sel, persist_cfg, true);
}

bool zvp_lut_slot_is_modified(const zvp_lut_ctx_t *ctx, uint32_t sel)
{
    if (!ctx || sel == 0u || sel >= ctx->num_slots) {
        return false;
    }
    const zvp_lut_adjust_t *a = &ctx->adjust[sel];
    const zvp_lut_adjust_t *d = &ctx->default_adjust[sel];
    if (a->contrast != d->contrast || a->saturation != d->saturation ||
        a->highlights != d->highlights || a->shadows != d->shadows ||
        a->fade != d->fade || a->strength10 != d->strength10) {
        return true;
    }
    return ctx->sharpness[sel] != ZVP_LUT_SHARPNESS_NATIVE;
}

int zvp_lut_begin_fine_tune(zvp_lut_ctx_t *ctx, uint32_t sel)
{
    if (!ctx || !ctx->initialized) {
        return -EINVAL;
    }
    if (!ctx->bundle_ready) {
        int brc = zvp_lut_load_bundle_and_cfg(ctx);
        if (brc != 0) {
            return brc;
        }
    }
    if (sel == 0u || sel >= ctx->num_slots) {
        return -EINVAL;
    }
    if (ctx->active_sel != sel || !ctx->kmod_loaded) {
        int src = zvp_lut_select_slot(ctx, sel, false, true);
        if (src != 0) {
            return src;
        }
    }
    ctx->fine_editing = true;
    ctx->fine_sel = sel;
    ctx->fine_saved_adjust = ctx->adjust[sel];
    ctx->fine_saved_sharpness = ctx->sharpness[sel];
    return 0;
}

int zvp_lut_adjust_param_step(zvp_lut_ctx_t *ctx, zvp_lut_param_t param, int delta)
{
    if (!ctx || !ctx->bundle_ready || delta == 0) {
        return -EINVAL;
    }
    uint32_t sel = ctx->fine_editing ? ctx->fine_sel : ctx->active_sel;
    if (sel == 0u || sel >= ctx->num_slots) {
        return -EINVAL;
    }

    if (param == ZVP_LUT_PARAM_SHARPNESS) {
        int8_t cur = ctx->sharpness[sel];
        int32_t base;
        if (cur == ZVP_LUT_SHARPNESS_NATIVE) {
            int32_t ref = (ctx->native_sharpness != ZVP_LUT_SHARPNESS_NATIVE)
                              ? (int32_t)ctx->native_sharpness
                              : 0;
            base = ref + delta;
            if (base < ZVP_LUT_SHARPNESS_MIN) base = ZVP_LUT_SHARPNESS_MIN;
            if (base > ZVP_LUT_SHARPNESS_MAX) base = ZVP_LUT_SHARPNESS_MAX;
            ctx->sharpness[sel] = (int8_t)base;
        } else {
            int32_t nv = (int32_t)cur + delta;
            if (nv < ZVP_LUT_SHARPNESS_MIN) {
                /* 最小档(-5)继续下调即切回"原生"跟随相机 */
                ctx->sharpness[sel] = ZVP_LUT_SHARPNESS_NATIVE;
            } else if (nv > ZVP_LUT_SHARPNESS_MAX) {
                ctx->sharpness[sel] = ZVP_LUT_SHARPNESS_MAX;
            } else {
                ctx->sharpness[sel] = (int8_t)nv;
            }
        }
        refresh_preview_if_needed(ctx, sel);
        return ctx->last_preview.rc;
    }

    zvp_lut_adjust_t next = ctx->adjust[sel];
    int v = 0, lo = -9, hi = 9;
    switch (param) {
        case ZVP_LUT_PARAM_STRENGTH:
            lo = 1; hi = 10; v = (int)next.strength10 + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.strength10 = (uint8_t)v;
            break;
        case ZVP_LUT_PARAM_CONTRAST:
            v = (int)next.contrast + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.contrast = (int8_t)v;
            break;
        case ZVP_LUT_PARAM_HIGHLIGHTS:
            v = (int)next.highlights + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.highlights = (int8_t)v;
            break;
        case ZVP_LUT_PARAM_SHADOWS:
            v = (int)next.shadows + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.shadows = (int8_t)v;
            break;
        case ZVP_LUT_PARAM_FADE:
            lo = 0; hi = 9; v = (int)next.fade + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.fade = (uint8_t)v;
            break;
        case ZVP_LUT_PARAM_SATURATION:
            v = (int)next.saturation + delta;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            next.saturation = (int8_t)v;
            break;
        default:
            return -EINVAL;
    }

    ctx->adjust[sel] = next;
    int rc = zvp_lut_ensure_kernel_ready(ctx);
    if (rc == 0) {
        rc = apply_kernel_selection(ctx, sel);
        if (rc == 0) {
            refresh_preview_if_needed(ctx, sel);
        }
    }
    return rc;
}

int zvp_lut_reset_fine_tune_draft(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->bundle_ready) {
        return -EINVAL;
    }
    uint32_t sel = ctx->fine_editing ? ctx->fine_sel : ctx->active_sel;
    if (sel == 0u || sel >= ctx->num_slots) {
        return -EINVAL;
    }
    ctx->adjust[sel] = ctx->default_adjust[sel];
    ctx->sharpness[sel] = ZVP_LUT_SHARPNESS_NATIVE;
    return zvp_lut_select_slot(ctx, sel, false, true);
}

int zvp_lut_commit_fine_tune(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->bundle_ready) {
        return -EINVAL;
    }
    uint32_t sel = ctx->fine_editing ? ctx->fine_sel : ctx->active_sel;
    if (sel > 0u && sel < ctx->num_slots) {
        int rc = zvp_lut_select_slot(ctx, sel, true, false);
        if (rc != 0) {
            return rc;
        }
    } else {
        (void)save_cfg_to_disk(ctx);
    }
    ctx->fine_editing = false;
    ctx->fine_sel = 0u;
    return ctx->last_cfg_rc;
}

int zvp_lut_cancel_fine_tune(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->bundle_ready) {
        return -EINVAL;
    }
    if (!ctx->fine_editing) {
        return 0;
    }
    uint32_t sel = ctx->fine_sel;
    if (sel > 0u && sel < ctx->num_slots) {
        ctx->adjust[sel] = ctx->fine_saved_adjust;
        ctx->sharpness[sel] = ctx->fine_saved_sharpness;
        if (ctx->kmod_loaded) {
            (void)apply_kernel_selection(ctx, sel);
            refresh_preview_if_needed(ctx, sel);
        }
    }
    ctx->fine_editing = false;
    ctx->fine_sel = 0u;
    return 0;
}

int zvp_lut_flush_pending_cfg(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->initialized || !ctx->bundle_ready || !ctx->cfg_dirty) {
        return 0;
    }
    if (ctx->fine_editing) {
        return 0; /* 微调草稿期间不自动覆盖原厂已保存参数，仅在保存时提交 */
    }
    return save_cfg_to_disk(ctx);
}

int zvp_lut_shutdown_reset(zvp_lut_ctx_t *ctx)
{
    if (!ctx || !ctx->initialized) {
        return 0;
    }
    ctx->fine_editing = false;
    ctx->cfg_dirty = false;
    if (!ctx->kmod_loaded) {
        ctx->active_sel = 0u;
        if (ctx->ops.atomic_write_file) {
            (void)save_cfg_to_disk(ctx);
        }
        return 0;
    }
    int rc = apply_kernel_selection(ctx, 0u);
    ctx->active_sel = 0u;
    refresh_preview_if_needed(ctx, 0u);
    if (ctx->ops.atomic_write_file) {
        (void)save_cfg_to_disk(ctx);
    }
    return rc;
}

static void find_saved_adjust_for_id(const zvp_lut_ctx_t *ctx, const char *id, zvp_lut_adjust_t *io_adj)
{
    if (!ctx || !id || !id[0] || !io_adj) {
        return;
    }
    for (uint32_t i = 1u; i < ctx->num_slots; i++) {
        if (id_equal(ctx->look_ids[i], id) && zvp_lut_adjust_valid(ctx->adjust[i])) {
            *io_adj = ctx->adjust[i];
            return;
        }
    }
    if (!ctx->ops.read_file) {
        return;
    }
    zvp_lut_cfg_disk_t cfg;
    size_t got = 0u;
    memset(&cfg, 0, sizeof(cfg));
    if (ctx->ops.read_file(ZVP_LUT_CFG_PATH, &cfg, sizeof(cfg), &got) != 0) {
        return;
    }
    bool valid = (got == sizeof(cfg) && cfg_v5_valid(&cfg)) ||
                 (got == (size_t)(ZVP_LUT_CFG_V4_CRC_OFF + 4u) &&
                  cfg.magic == ZVP_LUT_CFG_MAGIC && cfg.version == 4u &&
                  rd_le32((const uint8_t *)&cfg + ZVP_LUT_CFG_V4_CRC_OFF) ==
                      zvp_lut_crc32(&cfg, ZVP_LUT_CFG_V4_CRC_OFF));
    if (!valid) {
        return;
    }
    for (uint32_t j = 0u; j < ZVP_LUT_VISIBLE_LOOKS; j++) {
        if (id_equal(cfg.looks[j].id, id) && zvp_lut_adjust_valid(cfg.looks[j].adjust)) {
            *io_adj = cfg.looks[j].adjust;
            return;
        }
    }
}

int zvp_lut_scan_sd_bundles(zvp_lut_ctx_t *ctx,
                            zvp_lut_sd_item_t *out_items,
                            uint32_t max_items,
                            uint32_t *out_count)
{
    if (out_count) *out_count = 0u;
    if (!ctx || !ctx->initialized || !out_items || max_items == 0u) {
        return -EINVAL;
    }
    memset(out_items, 0, sizeof(zvp_lut_sd_item_t) * max_items);

    uint32_t sd_cnt = 0u;
    uint32_t cap_for_sd = (max_items > 1u) ? (max_items - 1u) : 0u;
    int scan_rc = 0;
    if (ctx->ops.scan_sd_bundles && cap_for_sd > 0u) {
        scan_rc = ctx->ops.scan_sd_bundles(out_items, cap_for_sd, &sd_cnt);
        if (sd_cnt > cap_for_sd) {
            sd_cnt = cap_for_sd;
        }
    }

    uint32_t emb_cnt = 0u;
    if (ctx->ops.default_bundle_data && ctx->ops.default_bundle_len >= ZVP_LUT_BUNDLE_HEADER) {
        emb_cnt = rd_le32((const uint8_t *)ctx->ops.default_bundle_data + 12u);
        if (emb_cnt > ZVP_LUT_VISIBLE_LOOKS) emb_cnt = ZVP_LUT_VISIBLE_LOOKS;
    }

    zvp_lut_sd_item_t *emb = &out_items[sd_cnt];
    snprintf(emb->name, sizeof(emb->name), "%s", "内置默认");
    snprintf(emb->path, sizeof(emb->path), "%s", "__EMBEDDED__");
    emb->size = (uint32_t)ctx->ops.default_bundle_len;
    emb->look_count = (uint8_t)emb_cnt;
    emb->is_embedded = true;

    if (out_count) *out_count = sd_cnt + 1u;
    return scan_rc;
}

int zvp_lut_hot_import_bytes(zvp_lut_ctx_t *ctx,
                             const uint8_t *bytes,
                             size_t size,
                             int32_t preferred_sel,
                             bool is_embedded)
{
    if (!ctx || !ctx->initialized || !bytes) {
        return -EINVAL;
    }
    uint32_t new_count = 0u, new_digest = 0u;
    int vrc = zvp_lut_validate_bundle(bytes, size, &new_count, &new_digest);
    if (vrc != 0) {
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "滤镜包校验失败 rc=%d", vrc);
        return vrc;
    }

    /* 若已有相同摘要在用，仍按需求确保落盘并激活预览 */
    if (ctx->bundle_ready && ctx->bundle_digest == new_digest) {
        uint32_t sel = (preferred_sel >= 0 && (uint32_t)preferred_sel <= new_count)
                           ? (uint32_t)preferred_sel
                           : ((ctx->active_sel > 0u && ctx->active_sel <= new_count) ? ctx->active_sel : 1u);
        ctx->bundle_from_embedded = is_embedded;
        if (ctx->ops.atomic_write_file) {
            (void)ctx->ops.atomic_write_file(ZVP_LUT_DIR_PATH, ZVP_LUT_BUNDLE_PATH,
                                             ctx->bundle_bytes, ctx->bundle_size);
        }
        return zvp_lut_select_slot(ctx, sel, true, true);
    }

    char prev_id[ZVP_LUT_ID_BYTES];
    memset(prev_id, 0, sizeof(prev_id));
    if (ctx->active_sel > 0u && ctx->active_sel < ctx->num_slots) {
        memcpy(prev_id, ctx->look_ids[ctx->active_sel], sizeof(prev_id));
    }

    uint32_t next_sel = 1u;
    if (preferred_sel >= 0 && (uint32_t)preferred_sel <= new_count) {
        next_sel = (uint32_t)preferred_sel;
    } else if (prev_id[0] != '\0') {
        for (uint32_t i = 1u; i <= new_count; i++) {
            const uint8_t *e = bytes + ZVP_LUT_BUNDLE_HEADER + (i - 1u) * ZVP_LUT_BUNDLE_ENTRY;
            if (id_equal((const char *)e, prev_id)) {
                next_sel = i;
                break;
            }
        }
    }

    zvp_lut_adjust_t next_adj;
    memset(&next_adj, 0, sizeof(next_adj));
    next_adj.strength10 = 10u;
    if (next_sel > 0u && next_sel <= new_count) {
        const uint8_t *e = bytes + ZVP_LUT_BUNDLE_HEADER + (next_sel - 1u) * ZVP_LUT_BUNDLE_ENTRY;
        next_adj.contrast   = (int8_t)e[104];
        next_adj.highlights = (int8_t)e[105];
        next_adj.shadows    = (int8_t)e[106];
        next_adj.saturation = (int8_t)e[107];
        next_adj.fade       = e[108];
        next_adj.strength10 = e[109];
        find_saved_adjust_for_id(ctx, (const char *)e, &next_adj);
    }

    bool hot_applied = false;
    if (ctx->kmod_loaded && ctx->catalog_uploaded) {
        if (!ctx->ops.sys_write_fd) {
            return -ENOSYS;
        }
        long rc = catalog_cmd(ctx, ZVP_LUT_CAT_HOT_BEGIN | new_count, (uintptr_t)ctx->bundle_digest);
        if (rc == 0) {
            rc = upload_packets_to_kernel(ctx, bytes, new_count);
        }
        if (rc == 0) {
            rc = catalog_cmd(ctx, ZVP_LUT_CAT_HOT_SELECT | next_sel,
                             (uintptr_t)zvp_lut_pack_adjust(next_adj));
        }
        if (rc == 0) {
            rc = catalog_cmd(ctx, ZVP_LUT_CAT_HOT_COMMIT, (uintptr_t)new_digest);
        }
        if (rc != 0) {
            (void)catalog_cmd(ctx, ZVP_LUT_CAT_HOT_ABORT, 0u);
            ctx->last_catalog_rc = (int)rc;
            snprintf(ctx->last_msg, sizeof(ctx->last_msg), "热导入失败 rc=%d", (int)rc);
            return (int)rc;
        }
        ctx->last_catalog_rc = 0;
        ctx->last_apply_rc = 0;
        hot_applied = true;
    }

    int prc = parse_bundle_into_ctx(ctx, bytes, size);
    if (prc != 0) {
        return prc;
    }
    ctx->bundle_from_embedded = is_embedded;
    ctx->fine_editing = false;
    ctx->fine_sel = 0u;
    load_cfg_from_disk(ctx);
    ctx->active_sel = next_sel;
    if (next_sel > 0u) {
        ctx->adjust[next_sel] = next_adj;
    }

    if (ctx->ops.atomic_write_file) {
        (void)ctx->ops.atomic_write_file(ZVP_LUT_DIR_PATH, ZVP_LUT_BUNDLE_PATH,
                                         ctx->bundle_bytes, ctx->bundle_size);
    }
    (void)save_cfg_to_disk(ctx);

    if (hot_applied) {
        refresh_preview_if_needed(ctx, next_sel);
    } else if (next_sel > 0u) {
        int src = zvp_lut_select_slot(ctx, next_sel, true, true);
        if (src != 0) {
            return src;
        }
    }

    snprintf(ctx->last_msg, sizeof(ctx->last_msg), "已热导入 %u 款 (%s)",
             (unsigned)new_count, is_embedded ? "内置默认" : "SD卡");
    return 0;
}

int zvp_lut_hot_import_item(zvp_lut_ctx_t *ctx, const zvp_lut_sd_item_t *item)
{
    if (!ctx || !ctx->initialized || !item) {
        return -EINVAL;
    }
    if (item->is_embedded) {
        if (!ctx->ops.default_bundle_data || ctx->ops.default_bundle_len == 0u) {
            return -ENOENT;
        }
        return zvp_lut_hot_import_bytes(ctx,
                                        (const uint8_t *)ctx->ops.default_bundle_data,
                                        ctx->ops.default_bundle_len,
                                        -1, true);
    }

    int (*reader)(const char *, void *, size_t, size_t *) =
        ctx->ops.read_sd_file ? ctx->ops.read_sd_file : ctx->ops.read_file;
    if (!reader || !item->path[0]) {
        return -ENOSYS;
    }

    uint8_t *tmp = (uint8_t *)malloc(ZVP_LUT_BUNDLE_MAX_BYTES);
    if (!tmp) {
        return -ENOMEM;
    }
    size_t got = 0u;
    int rrc = reader(item->path, tmp, ZVP_LUT_BUNDLE_MAX_BYTES, &got);
    if (rrc != 0) {
        free(tmp);
        snprintf(ctx->last_msg, sizeof(ctx->last_msg), "读取失败 rc=%d", rrc);
        return rrc;
    }
    int irc = zvp_lut_hot_import_bytes(ctx, tmp, got, -1, false);
    free(tmp);
    return irc;
}
