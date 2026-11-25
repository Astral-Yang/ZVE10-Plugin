#ifndef ZVP_SOUND_H
#define ZVP_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZVP_SOUND_MAX_ITEMS      32u
#define ZVP_SOUND_NAME_LEN       64u
#define ZVP_SOUND_PATH_LEN       384u
#define ZVP_SOUND_MAX_WAV_BYTES  (1024u * 1024u)
#define ZVP_SOUND_TARGET_RATE    24000u
#define ZVP_SOUND_MAX_SECONDS    5u
#define ZVP_SOUND_F0_BLOCK_BYTES 32u
#define ZVP_SOUND_F0_SAMPLES_PER_BLOCK 31u

#define ZVP_SOUND_ACTIVE_BIN_PATH "/tmp/zvp_shutter.bin"
#define ZVP_SOUND_SLOT_PREFIX     "/usr/share/avsystemsound/zvp_slot_"
#define ZVP_SOUND_CACHE_SLOTS     4u

typedef struct {
    bool     occupied;
    uint8_t  slot;
    bool     is_builtin;
    uint8_t  builtin_id;
    char     path[ZVP_SOUND_PATH_LEN];
    uint32_t f0_bytes;
    uint32_t last_seq;
} zvp_sound_slot_cache_t;

typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t frames;
} zvp_wav_format_t;

typedef struct {
    char name[ZVP_SOUND_NAME_LEN];
    char path[ZVP_SOUND_PATH_LEN];
    bool is_builtin;
    uint8_t builtin_id;
} zvp_sound_item_t;

typedef struct {
    zvp_sound_item_t items[ZVP_SOUND_MAX_ITEMS];
    uint32_t count;
    uint32_t selected_idx;
    char selected_name[ZVP_SOUND_NAME_LEN];

    bool scanned;
    bool prepared;
    uint8_t active_slot;
    uint32_t slot_cursor;
    uint32_t cache_seq;
    uint32_t evict_count;
    zvp_sound_slot_cache_t slot_cache[ZVP_SOUND_CACHE_SLOTS];
    uint32_t last_f0_bytes;
    int32_t last_error;
    uint32_t play_count;
    uint64_t last_play_ms;

    char override_dir[ZVP_SOUND_PATH_LEN];
    char active_bin_path[ZVP_SOUND_PATH_LEN];
    char last_source_label[64];
} zvp_sound_mgr_t;

/* WAV 解析与 Sony 24kHz F0 32 字节块编码 */
int zvp_wav_parse(const uint8_t *wav, size_t len, zvp_wav_format_t *out);
int zvp_wav_to_f0(const uint8_t *wav,
                  size_t wav_len,
                  uint8_t *out_f0,
                  size_t out_cap,
                  size_t *out_len);

/* 生成内置合成快门音效 F0 数据 (preset_id: 0=清脆快门, 1=电子快门) */
size_t zvp_sound_build_builtin_f0(uint8_t preset_id, uint8_t *out_f0, size_t out_cap);

/* 快门音管理接口 */
void zvp_sound_init(zvp_sound_mgr_t *mgr);
void zvp_sound_scan(zvp_sound_mgr_t *mgr);
bool zvp_sound_select(zvp_sound_mgr_t *mgr, uint32_t idx);
bool zvp_sound_select_by_name(zvp_sound_mgr_t *mgr, const char *name);
void zvp_sound_prev(zvp_sound_mgr_t *mgr);
void zvp_sound_next(zvp_sound_mgr_t *mgr);
bool zvp_sound_prepare_current(zvp_sound_mgr_t *mgr);
/* UI/预览线程调用：如尚未就绪可先同步准备再触发播放 */
bool zvp_sound_play(zvp_sound_mgr_t *mgr, uint64_t now_ms);
/* 输入快路径调用：仅在已就绪时零阻塞触发播放，绝不执行磁盘或重采样 I/O */
bool zvp_sound_play_prepared(zvp_sound_mgr_t *mgr, uint64_t now_ms);
/* 全量恢复系统 SoundTable 原始槽位、关闭重映射并清理临时音频缓存 */
void zvp_sound_reset(zvp_sound_mgr_t *mgr);
void zvp_sound_hook_reset(void);
bool zvp_sound_hook_unregist_adf(uint8_t adf_id);

/* 固件运行时桥接回调（由 zvp_entry.c 注入或宿主空实现） */
void zvp_sound_on_sys_data(void *sys_snd_data);
void zvp_sound_on_obj_if(void *obj_if);
bool zvp_sound_has_active_remap(void);
const char *zvp_sound_remap_fopen_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_SOUND_H */
