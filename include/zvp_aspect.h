#ifndef ZVP_ASPECT_H
#define ZVP_ASPECT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZVP_ASPECT_PRESET_COUNT 12u

/* 实机 VdfCh(41) 验证：ZV-E10 拍照实时取景有效画面为顶部 640x428（底部 52px 为快门/光圈/ISO 参数栏） */
#define ZVP_LIVEVIEW_DEFAULT_X 0
#define ZVP_LIVEVIEW_DEFAULT_Y 0
#define ZVP_LIVEVIEW_DEFAULT_W 640
#define ZVP_LIVEVIEW_DEFAULT_H 428

/* 遮罩外观风格 */
typedef enum {
    ZVP_ASPECT_STYLE_FRAME_ONLY  = 0, /* 仅画构图框线，不压暗四周 */
    ZVP_ASPECT_STYLE_DIM_45      = 1, /* 45% 半透明黑色遮幅（推荐默认） */
    ZVP_ASPECT_STYLE_DIM_75      = 2, /* 75% 深压暗黑色遮幅 */
    ZVP_ASPECT_STYLE_SOLID_BLACK = 3, /* 100% 纯黑电影遮幅 */
    ZVP_ASPECT_STYLE_COUNT
} zvp_aspect_style_t;

/* 辅助构图线模式 */
typedef enum {
    ZVP_ASPECT_GUIDE_NONE        = 0, /* 无内框线（纯遮幅时常用） */
    ZVP_ASPECT_GUIDE_BORDER      = 1, /* 高对比白底细边框 */
    ZVP_ASPECT_GUIDE_THIRDS      = 2, /* 边框 + 三分九宫格细线 */
    ZVP_ASPECT_GUIDE_COUNT
} zvp_aspect_guide_t;

/* 取景画面基准模式（替代屏幕尺寸：以实际拍摄画面、去掉最下层参数条为基准） */
typedef enum {
    ZVP_ASPECT_PAR_AUTO = 0, /* 自动跟随实拍画面（未捕获前默认 640x428 去底栏） */
    ZVP_ASPECT_PAR_1_1  = 1, /* 锁定实拍去底栏 640x428 (3:2 标准取景) */
    ZVP_ASPECT_PAR_9_8  = 2, /* 锁定全屏无底栏 640x480 (DISP 无参数模式) */
    ZVP_ASPECT_PAR_4_3  = 3, /* 锁定去底栏 16:9 画面 640x360 (y=34..394) */
    ZVP_ASPECT_PAR_COUNT
} zvp_aspect_par_mode_t;

typedef struct zvp_aspect_preset {
    const char *label; /* UI 短名称，如 "21:9", "2.35:1" */
    const char *desc;  /* 说明文字 */
    uint16_t    ratio_num;
    uint16_t    ratio_den;
} zvp_aspect_preset_t;

typedef struct zvp_aspect_rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} zvp_aspect_rect_t;

typedef struct zvp_aspect_layout {
    bool valid;
    zvp_aspect_rect_t view_rect;  /* 基准 LiveView 区域 */
    zvp_aspect_rect_t frame_rect; /* 居中裁剪后有效构图框 */
    zvp_aspect_rect_t bar_top;
    zvp_aspect_rect_t bar_bottom;
    zvp_aspect_rect_t bar_left;
    zvp_aspect_rect_t bar_right;
    uint16_t par_num;
    uint16_t par_den;
} zvp_aspect_layout_t;

/* 来自底层 VdfCh / ViewManager 的运行时实时观测快照 */
typedef struct zvp_aspect_hw_state {
    bool vdf_rect_valid;
    zvp_aspect_rect_t vdf_rect; /* VdfCh setYUVLayout/getYuvLayout 最新目标区域 */
    bool panel_aspect_valid;
    bool panel_is_16_9;         /* CmnViewFeatureManager::isPanelAspect16_9 或 VdfCh */
    uint8_t vdf_panel_raw[2];   /* VdfCh::getPanelAspect 原始字节 [+8, +9] */
    bool camera_in_menu;        /* 原厂 MENU 开启 */
    bool camera_in_playback;    /* 原厂回放页开启 */
} zvp_aspect_hw_state_t;

/* 纵横比功能独立私有状态（与原厂 3:2 设置彻底解耦） */
typedef struct zvp_aspect_ctx {
    bool                  enabled;
    uint8_t               preset_idx;
    zvp_aspect_style_t    style;
    zvp_aspect_guide_t    guide;
    zvp_aspect_par_mode_t par_mode;
    zvp_aspect_hw_state_t hw;
    zvp_aspect_layout_t   layout;
} zvp_aspect_ctx_t;

void zvp_aspect_init(zvp_aspect_ctx_t *ctx);
void zvp_aspect_reset_defaults(zvp_aspect_ctx_t *ctx);

const zvp_aspect_preset_t *zvp_aspect_get_presets(size_t *out_count);
const zvp_aspect_preset_t *zvp_aspect_get_current_preset(const zvp_aspect_ctx_t *ctx);

void zvp_aspect_set_enabled(zvp_aspect_ctx_t *ctx, bool enable);
void zvp_aspect_select_preset(zvp_aspect_ctx_t *ctx, uint8_t idx);
void zvp_aspect_cycle_style(zvp_aspect_ctx_t *ctx, int step);
void zvp_aspect_cycle_guide(zvp_aspect_ctx_t *ctx, int step);
void zvp_aspect_cycle_par_mode(zvp_aspect_ctx_t *ctx, int step);

const char *zvp_aspect_style_name(zvp_aspect_style_t style);
uint8_t     zvp_aspect_style_opa(zvp_aspect_style_t style);
const char *zvp_aspect_guide_name(zvp_aspect_guide_t guide);
const char *zvp_aspect_par_mode_name(zvp_aspect_par_mode_t mode);

/* 判断当前是否在拍摄叠加显示上下文（隐藏于原厂 MENU 与相册回放，但菜单打开预览时除外） */
bool zvp_aspect_should_display(const zvp_aspect_ctx_t *ctx, bool plugin_menu_visible);

/* 更新来自 VdfCh 与视图状态机的硬件快照并重算布局 */
void zvp_aspect_update_vdf_rect(zvp_aspect_ctx_t *ctx, int16_t x, int16_t y, int16_t w, int16_t h);
void zvp_aspect_update_panel_aspect(zvp_aspect_ctx_t *ctx, bool is_16_9, uint8_t raw0, uint8_t raw1);
void zvp_aspect_update_camera_ui_state(zvp_aspect_ctx_t *ctx, bool in_menu, bool in_playback);

/* 纯整数坐标重算：给出物理有效区与四块遮幅矩形（兼容 180° 翻转对称投影） */
void zvp_aspect_recompute(zvp_aspect_ctx_t *ctx, bool ui_rotated_180);

/* 底层纯计算函数（便于独立单元测试调用） */
void zvp_aspect_compute_layout(const zvp_aspect_rect_t *view_rect,
                               uint16_t par_num,
                               uint16_t par_den,
                               uint16_t ratio_num,
                               uint16_t ratio_den,
                               bool ui_rotated_180,
                               zvp_aspect_layout_t *out_layout);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_ASPECT_H */
