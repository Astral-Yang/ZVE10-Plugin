#ifndef ZVP_MENU_TOUCH_H
#define ZVP_MENU_TOUCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sony ZV-E10 原厂菜单 (PAS_MenuListTab / PAS_MenuListPopup) & Fn 菜单全界面触控
 * 基于 bionz-x-research ZV-E10 (FW 2.02/2.03) 实证偏移 + LD_PRELOAD 动态基址集成
 * ========================================================================== */

#define ZVP_MTF_ACT_NONE         0u
#define ZVP_MTF_ACT_TAB          1u
#define ZVP_MTF_ACT_PAGE         2u
#define ZVP_MTF_ACT_ROW_SELECT   3u
#define ZVP_MTF_ACT_ROW_ENTER    4u
#define ZVP_MTF_ACT_SWIPE_PAGE   5u
#define ZVP_MTF_ACT_POPUP_SELECT 6u
#define ZVP_MTF_ACT_POPUP_SCROLL 7u
#define ZVP_MTF_ACT_POPUP_CANCEL 8u
#define ZVP_MTF_ACT_FN_SELECT    10u
#define ZVP_MTF_ACT_FN_ENTER     11u
#define ZVP_MTF_ACT_FN_CANCEL    12u

typedef enum {
    ZVP_MTF_GESTURE_TAP = 0,
    ZVP_MTF_GESTURE_SWIPE_LEFT = 1,
    ZVP_MTF_GESTURE_SWIPE_RIGHT = 2,
    ZVP_MTF_GESTURE_SWIPE_UP = 3,
    ZVP_MTF_GESTURE_SWIPE_DOWN = 4
} zvp_mtf_gesture_t;

typedef struct zvp_menu_touch_ops {
    uintptr_t (*find_module_base)(const char *soname);
    int       (*read_mem)(uintptr_t addr, void *out_buf, size_t len);
    int       (*write_mem)(uintptr_t addr, const void *buf, size_t len);
    bool      (*should_handle_fn)(void);
} zvp_menu_touch_ops_t;

typedef struct zvp_menu_touch_stats {
    bool     hooks_installed;
    bool     enabled;
    uint32_t last_action;
    uint32_t last_err;
    uint32_t events_handled;
    uintptr_t libobj_base;
    uintptr_t view2_base;
    uintptr_t view4_base;
    uintptr_t caution_base;
} zvp_menu_touch_stats_t;

/* 纯逻辑辅助接口（宿主+真机通用，供单元测试直接覆盖） */
int zvp_menu_touch_fn_tile_index(int16_t x, int16_t y);
zvp_mtf_gesture_t zvp_menu_touch_classify_main_swipe(int dx, int dy, bool has_press);
zvp_mtf_gesture_t zvp_menu_touch_classify_popup_swipe(int dx, int dy, bool has_press);
int zvp_menu_touch_fn_ring_steps(uint32_t cur_idx, uint32_t target_idx, int *out_dir_action);

/* 真机运行期管理接口（在宿主测试中为空桩） */
int  zvp_menu_touch_init(const zvp_menu_touch_ops_t *ops);
void zvp_menu_touch_set_enabled(bool enabled);
bool zvp_menu_touch_is_enabled(void);
void zvp_menu_touch_uninstall(void);
void zvp_menu_touch_get_stats(zvp_menu_touch_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_MENU_TOUCH_H */
