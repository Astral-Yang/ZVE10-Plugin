#include "zvp_menu_touch.h"
#include "zvp_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int iabs_val(int v)
{
    return (v < 0) ? -v : v;
}

int zvp_menu_touch_fn_tile_index(int16_t x, int16_t y)
{
    /* y < 260 属于上方实时取景区域，点击触发取消退出 Fn 菜单 */
    if (y < 260) {
        return -1;
    }
    int col;
    if (x < 110) {
        col = 0;
    } else if (x < 215) {
        col = 1;
    } else if (x < 320) {
        col = 2;
    } else if (x < 425) {
        col = 3;
    } else if (x < 530) {
        col = 4;
    } else {
        col = 5;
    }
    int row = (y >= 370) ? 1 : 0;
    int idx = row * 6 + col;
    if (idx < 0) idx = 0;
    if (idx > 11) idx = 11;
    return idx;
}

zvp_mtf_gesture_t zvp_menu_touch_classify_main_swipe(int dx, int dy, bool has_press)
{
    if (!has_press) {
        return ZVP_MTF_GESTURE_TAP;
    }
    if (iabs_val(dx) >= 48 && iabs_val(dx) > 2 * iabs_val(dy)) {
        return (dx < 0) ? ZVP_MTF_GESTURE_SWIPE_LEFT : ZVP_MTF_GESTURE_SWIPE_RIGHT;
    }
    return ZVP_MTF_GESTURE_TAP;
}

zvp_mtf_gesture_t zvp_menu_touch_classify_popup_swipe(int dx, int dy, bool has_press)
{
    if (!has_press) {
        return ZVP_MTF_GESTURE_TAP;
    }
    if (iabs_val(dy) >= 36 && iabs_val(dy) > iabs_val(dx)) {
        return (dy < 0) ? ZVP_MTF_GESTURE_SWIPE_UP : ZVP_MTF_GESTURE_SWIPE_DOWN;
    }
    return ZVP_MTF_GESTURE_TAP;
}

int zvp_menu_touch_fn_ring_steps(uint32_t cur_idx, uint32_t target_idx, int *out_dir_action)
{
    uint32_t c = (cur_idx <= 11u) ? cur_idx : 0u;
    uint32_t t = (target_idx <= 11u) ? target_idx : 0u;
    int diff = ((int)t - (int)c + 12) % 12;
    if (diff <= 6) {
        if (out_dir_action) *out_dir_action = 1; /* RIGHT (+1) */
        return diff;
    }
    if (out_dir_action) *out_dir_action = 0;     /* LEFT (-1) */
    return 12 - diff;
}

#if defined(__arm__) && !defined(ZVP_HOST_TEST)

/* ============================================================================
 * Sony ZV-E10 FW 2.02 / 2.03 实证二进制偏移定义
 * 全部通过前导码比对 + 动态库基址解析后使用，失配即 Fail-Closed 拒装
 * ========================================================================== */

/* libObj.so */
#define MTF_OFF_HOOK_REL       0x00618000u
#define MTF_OFF_HOOK_PRS       0x0061810Cu
#define MTF_OFF_SYSTEM_ADDR    0x0145CB1Cu
#define MTF_OFF_GET_POS        0x006172CDu
#define MTF_OFF_LOOKUP         0x005F05AFu
#define MTF_OFF_IS_HIT         0x00613335u
#define MTF_OFF_INHERITED      0x00613BD1u
#define MTF_OFF_CREATE_EVT     0x0012C651u
#define MTF_OFF_PUSH_EVT       0x0012C62Du
#define MTF_OFF_VIEW_MGR_PTR   0x0149221Cu
#define MTF_OFF_VIEW_MGR_GOT   0x013DC7B4u

/* viewUnified2.so */
#define MTF_OFF_VTV_PAS_TAB    0x00C09E28u
#define MTF_OFF_ENTER          0x0086292Du
#define MTF_OFF_POPUP_PREV     0x00861B4Fu
#define MTF_OFF_POPUP_NEXT     0x00861B79u
#define MTF_OFF_REFRESH        0x008629F9u
#define MTF_OFF_CLOSE_POPUP    0x008620E1u
#define MTF_OFF_VTV_SET_MENU   0x00BA2C8Cu
#define MTF_OFF_ACTION_DISP    0x0030412Bu
#define MTF_OFF_FN_EXISTENCE   0x0059140Du

/* viewUnified4.so */
#define MTF_OFF_VTV_VIEW_FN    0x00317BD8u
#define MTF_OFF_VTV_VIEW_FN_S  0x00317D84u
#define MTF_OFF_FN_ACTION      0x0023249Fu /* +1 Thumb */
#define MTF_OFF_FN_SHOW_CURSOR 0x002284B5u /* +1 Thumb */
#define MTF_OFF_FN_UPDATE_TILE 0x0022C439u /* +1 Thumb */

/* CautionConfig.so */
#define MTF_OFF_VTV_NODE       0x00FAB968u
#define MTF_OFF_SEL_INDEX      0x00B8E127u
#define MTF_OFF_IDX_CHILD      0x00B8E0CBu
#define MTF_OFF_SET_INDEX      0x00B8E5EBu

#define MTF_EVT_KEY_ENTER      0x15010042u
#define MTF_EVT_KEY_FN         0x150100F2u
#define MTF_MAX_TABS           6u
#define MTF_MAX_PAGES          15u
#define MTF_MAX_ROWS           6u

struct mtf_point {
    float x;
    float y;
};

static const uint8_t k_orig_rel[12] = {
    0x2Du, 0xE9u, 0xF0u, 0x47u, 0x88u, 0xB0u, 0x4Fu, 0xF0u, 0x14u, 0x08u, 0x01u, 0x22u
};
static const uint8_t k_orig_prs[12] = {
    0x2Du, 0xE9u, 0xF0u, 0x47u, 0x88u, 0xB0u, 0x4Fu, 0xF0u, 0x14u, 0x09u, 0x01u, 0x22u
};

extern uint32_t zvp_mtf_orig_release(void *system, void *message);
extern uint32_t zvp_mtf_orig_press(void *system, void *message);
extern uint32_t zvp_mtf_rel_resume_slot;
extern uint32_t zvp_mtf_prs_resume_slot;

typedef struct zvp_mtf_ctx {
    bool     initialized;
    bool     hooks_installed;
    volatile bool enabled;
    volatile uint32_t inflight;
    uint32_t last_action;
    uint32_t last_err;
    uint32_t events_handled;
    int16_t  press_x;
    int16_t  press_y;
    bool     press_valid;

    uint32_t lib_base;
    uint32_t view2_base;
    uint32_t view4_base;
    uint32_t caution_base;
    uint32_t cached_menu;
    uint32_t cached_fn_view;

    zvp_menu_touch_ops_t ops;
} zvp_mtf_ctx_t;

static zvp_mtf_ctx_t s_mtf;

static inline uint32_t rd32(uint32_t addr, uint32_t off)
{
    return *(const volatile uint32_t *)(addr + off);
}

static inline uint8_t rd8(uint32_t addr, uint32_t off)
{
    return *(const volatile uint8_t *)(addr + off);
}

static inline void wr32(uint32_t addr, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(addr + off) = val;
}

static inline void wr8(uint32_t addr, uint32_t off, uint8_t val)
{
    *(volatile uint8_t *)(addr + off) = val;
}

static inline bool is_valid_ptr(uint32_t p)
{
    return p >= 0x00010000u && p < 0xC0000000u && (p & 3u) == 0u;
}

static bool find_heap_range(uint32_t *out_start, uint32_t *out_end)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) {
        return false;
    }
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "[heap]") != NULL && strstr(line, " rw") != NULL) {
            unsigned long s = 0, e = 0;
            if (sscanf(line, "%lx-%lx", &s, &e) == 2 && s < e) {
                *out_start = (uint32_t)s;
                *out_end = (uint32_t)e;
                found = true;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

static void refresh_dynamic_bases(void)
{
    if (!s_mtf.ops.find_module_base) {
        return;
    }
    if (!s_mtf.lib_base) {
        s_mtf.lib_base = (uint32_t)s_mtf.ops.find_module_base("libObj.so");
    }
    if (!s_mtf.view2_base) {
        s_mtf.view2_base = (uint32_t)s_mtf.ops.find_module_base("viewUnified2.so");
    }
    if (!s_mtf.caution_base) {
        s_mtf.caution_base = (uint32_t)s_mtf.ops.find_module_base("CautionConfig.so");
    }
    if (!s_mtf.view4_base) {
        s_mtf.view4_base = (uint32_t)s_mtf.ops.find_module_base("viewUnified4.so");
    }
}

static void sync_refresh_menu(uint32_t menu)
{
    void (*fn_refresh)(uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t))(uintptr_t)(s_mtf.view2_base + MTF_OFF_REFRESH);
    fn_refresh(menu, 0u);
}

static void push_async_event(uint32_t event_id, uint8_t ev_type)
{
    if (!event_id || !s_mtf.lib_base) {
        return;
    }
    void *(*create_evt)(uint32_t, uint8_t) =
        (void *(*)(uint32_t, uint8_t))(uintptr_t)(s_mtf.lib_base + MTF_OFF_CREATE_EVT);
    void (*push_evt)(void *) =
        (void (*)(void *))(uintptr_t)(s_mtf.lib_base + MTF_OFF_PUSH_EVT);
    void *ev = create_evt(event_id, ev_type);
    if (ev) {
        push_evt(ev);
    }
}

static void push_key_click(uint32_t key)
{
    push_async_event(key, 4u);
    push_async_event(key, 2u);
}

static uint32_t get_view_mgr(void)
{
    uint32_t vm = rd32(s_mtf.lib_base + MTF_OFF_VIEW_MGR_PTR, 0u);
    if (!is_valid_ptr(vm)) {
        uint32_t got_vm = rd32(s_mtf.lib_base + MTF_OFF_VIEW_MGR_GOT, 0u);
        if (is_valid_ptr(got_vm)) {
            vm = rd32(got_vm, 0u);
        }
    }
    return is_valid_ptr(vm) ? vm : 0u;
}

static uint32_t find_setting_menu_view(uint32_t menu)
{
    uint32_t vid = rd32(menu, 0x14Cu);
    uint32_t vm = get_view_mgr();
    if (!vm) {
        return 0u;
    }
    int cnt = (int)rd32(vm, 0u);
    if (cnt <= 0 || cnt > 128) {
        return 0u;
    }
    uint32_t vtv = s_mtf.view2_base + MTF_OFF_VTV_SET_MENU;

    uint32_t views_arr = rd32(vm, 4u);
    if (is_valid_ptr(views_arr)) {
        for (int i = 0; i < cnt; i++) {
            uint32_t vwrap = rd32(views_arr, (uint32_t)i * 4u);
            if (is_valid_ptr(vwrap)) {
                if (rd32(vwrap, 0u) == vtv) {
                    return vwrap;
                }
                uint32_t inner = rd32(vwrap, 0x18u);
                if (is_valid_ptr(inner) && rd32(inner, 0u) == vtv) {
                    return inner;
                }
            }
        }
    }

    uint32_t entries = rd32(vm, 0x20u);
    if (is_valid_ptr(entries)) {
        for (int i = 0; i < cnt; i++) {
            uint32_t ent = entries + (uint32_t)i * 16u;
            uint32_t vwrap = rd32(ent, 4u);
            if (!is_valid_ptr(vwrap) || (vid && rd32(ent, 8u) != vid)) {
                continue;
            }
            if (rd32(vwrap, 0u) == vtv) {
                return vwrap;
            }
            uint32_t inner = rd32(vwrap, 0x18u);
            if (is_valid_ptr(inner) && rd32(inner, 0u) == vtv) {
                return inner;
            }
            return vwrap;
        }
    }
    return 0u;
}

static void handle_popup_touch(uint32_t menu, int16_t x, int16_t y, int dx, int dy, bool has_press)
{
    uint32_t lib = s_mtf.lib_base;
    uint32_t view = s_mtf.view2_base;
    uint32_t caution = s_mtf.caution_base;

    uint32_t popup = menu + 0xFB20u;
    uint32_t pnode = rd32(popup, 0x6490u);
    if (!is_valid_ptr(pnode)) {
        s_mtf.last_err = 201u;
        return;
    }
    uint32_t pvtv = rd32(pnode, 0u);
    if (pvtv < caution || pvtv >= (caution + 0x01300000u)) {
        s_mtf.last_err = 202u;
        return;
    }

    int (*is_hit)(uint32_t, const struct mtf_point *) =
        (int (*)(uint32_t, const struct mtf_point *))(uintptr_t)(lib + MTF_OFF_IS_HIT);
    void (*close_popup)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(view + MTF_OFF_CLOSE_POPUP);
    void (*popup_up)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(view + MTF_OFF_POPUP_PREV);
    void (*popup_down)(uint32_t) =
        (void (*)(uint32_t))(uintptr_t)(view + MTF_OFF_POPUP_NEXT);
    int (*set_node_index)(uint32_t, unsigned) =
        (int (*)(uint32_t, unsigned))(uintptr_t)(caution + MTF_OFF_SET_INDEX);

    unsigned total = rd32(pnode, 8u);
    unsigned vis_cnt = rd8(popup, 0x6480u);
    if (total < 1u || total > 128u || vis_cnt < 1u || vis_cnt > MTF_MAX_ROWS) {
        s_mtf.last_err = 203u;
        return;
    }

    zvp_mtf_gesture_t g = zvp_menu_touch_classify_popup_swipe(dx, dy, has_press);
    if (g == ZVP_MTF_GESTURE_SWIPE_UP || g == ZVP_MTF_GESTURE_SWIPE_DOWN) {
        if (g == ZVP_MTF_GESTURE_SWIPE_UP) {
            popup_down(menu);
        } else {
            popup_up(menu);
        }
        s_mtf.last_action = ZVP_MTF_ACT_POPUP_SCROLL;
        s_mtf.events_handled++;
        return;
    }

    unsigned top = rd8(popup, 0x648Cu);
    struct mtf_point pt = { (float)x, (float)y };
    for (unsigned r = 0u; r < vis_cnt; r++) {
        unsigned opt = top + r + 1u;
        if (opt > total) {
            break;
        }
        if (is_hit(popup + 0x1070u + r * 0x398u, &pt) ||
            is_hit(popup + 0x36A8u + r * 0x220u, &pt)) {
            wr32(menu, 0x1FCu, top + r);
            (void)set_node_index(pnode, opt);
            close_popup(menu);
            s_mtf.last_action = ZVP_MTF_ACT_POPUP_SELECT;
            s_mtf.events_handled++;
            return;
        }
    }

    if ((!is_hit(popup + 0x64Cu, &pt) || x < 80 || x > 560 || y < 32 || y > 456) &&
        !is_hit(popup + 0x60D8u, &pt) && !is_hit(popup + 0x5C04u, &pt) &&
        !is_hit(popup + 0x2780u, &pt)) {
        close_popup(menu);
        s_mtf.last_action = ZVP_MTF_ACT_POPUP_CANCEL;
        s_mtf.events_handled++;
        return;
    }

    s_mtf.last_err = 204u;
}

static void handle_menu_touch(void *system_ptr, uint32_t mouse)
{
    refresh_dynamic_bases();
    uint32_t lib = s_mtf.lib_base;
    uint32_t view = s_mtf.view2_base;
    uint32_t caution = s_mtf.caution_base;
    if (!lib || !view || !caution) {
        s_mtf.last_err = 90u;
        return;
    }

    int16_t x = 0, y = 0;
    uint32_t sys_addr = system_ptr ? (uint32_t)(uintptr_t)system_ptr : (lib + MTF_OFF_SYSTEM_ADDR);
    void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) =
        (void (*)(uint32_t, unsigned, int16_t *, int16_t *))(uintptr_t)(lib + MTF_OFF_GET_POS);
    get_pos(sys_addr, mouse, &x, &y);

    bool has_press = s_mtf.press_valid;
    int dx = (int)x - (int)s_mtf.press_x;
    int dy = (int)y - (int)s_mtf.press_y;
    s_mtf.press_valid = false;

    if (x == 32767 || y == 32767) {
        s_mtf.last_err = 99u;
        return;
    }

    uint32_t expected_vtv = view + MTF_OFF_VTV_PAS_TAB;
    uint32_t err = ~0u;
    uint32_t (*lookup)(uint32_t, uint32_t, uint32_t, uint32_t *) =
        (uint32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t *))(uintptr_t)(lib + MTF_OFF_LOOKUP);
    uint32_t menu = lookup(0xAADCA2C7u, 0xDFDC8DDDu, 0xA056F604u, &err);
    if (!err && is_valid_ptr(menu)) {
        s_mtf.cached_menu = menu;
    } else {
        uint32_t cached = s_mtf.cached_menu;
        if (is_valid_ptr(cached) && rd32(cached, 0u) == expected_vtv && rd32(cached, 0x1Cu) == cached) {
            menu = cached;
        } else {
            s_mtf.last_err = 101u;
            return;
        }
    }

    if (rd32(menu, 0u) != expected_vtv || rd32(menu, 0x1Cu) != menu) {
        s_mtf.last_err = 103u;
        return;
    }

    int (*inherited_mode)(uint32_t) =
        (int (*)(uint32_t))(uintptr_t)(lib + MTF_OFF_INHERITED);
    if (rd8(menu, 0x21u) != 2u || inherited_mode(menu) > 2) {
        s_mtf.last_err = 105u;
        return;
    }

    uint32_t m_state = rd32(menu, 0x1E0u);
    uint8_t popup_open_byte = rd8(menu + 0xFB20u, 0x648Eu);
    if (m_state == 3u || popup_open_byte != 0u) {
        handle_popup_touch(menu, x, y, dx, dy, has_press);
        return;
    }
    if (m_state != 2u) {
        s_mtf.last_err = 107u;
        return;
    }

    uint32_t root = rd32(menu, 0x186ACu);
    uint32_t expected_node = caution + MTF_OFF_VTV_NODE;
    if (!is_valid_ptr(root) || rd32(root, 0u) != expected_node) {
        s_mtf.last_err = 108u;
        return;
    }

    int (*selected_index)(uint32_t, unsigned *) =
        (int (*)(uint32_t, unsigned *))(uintptr_t)(caution + MTF_OFF_SEL_INDEX);
    int (*indexed_child)(uint32_t, unsigned, uint32_t *) =
        (int (*)(uint32_t, unsigned, uint32_t *))(uintptr_t)(caution + MTF_OFF_IDX_CHILD);
    int (*set_selected_index)(uint32_t, unsigned) =
        (int (*)(uint32_t, unsigned))(uintptr_t)(caution + MTF_OFF_SET_INDEX);
    int (*is_hit)(uint32_t, const struct mtf_point *) =
        (int (*)(uint32_t, const struct mtf_point *))(uintptr_t)(lib + MTF_OFF_IS_HIT);
    int (*enter_selected)(uint32_t, uint32_t) =
        (int (*)(uint32_t, uint32_t))(uintptr_t)(view + MTF_OFF_ENTER);

    unsigned tabs = rd32(root, 8u), tab_sel = 0u;
    if (tabs < 1u || tabs > MTF_MAX_TABS ||
        selected_index(root, &tab_sel) != 1 || tab_sel < 1u || tab_sel > tabs) {
        s_mtf.last_err = 112u;
        return;
    }

    struct mtf_point pt = { (float)x, (float)y };
    zvp_mtf_gesture_t swipe = zvp_menu_touch_classify_main_swipe(dx, dy, has_press);
    uint32_t sm_view = find_setting_menu_view(menu);

    /* 1. 顶部标签切换（非滑动时） */
    if (swipe == ZVP_MTF_GESTURE_TAP) {
        for (unsigned i = 0u; i < tabs; i++) {
            if (is_hit(menu + 0x0B9Cu + i * 0x220u, &pt)) {
                if (i + 1u != tab_sel && set_selected_index(root, i + 1u) == 1) {
                    sync_refresh_menu(menu);
                }
                s_mtf.last_action = ZVP_MTF_ACT_TAB;
                s_mtf.events_handled++;
                return;
            }
        }
    }

    uint32_t tab_node = 0u;
    if (indexed_child(root, tab_sel, &tab_node) != 1 || !is_valid_ptr(tab_node) ||
        rd32(tab_node, 0u) != expected_node) {
        s_mtf.last_err = 115u;
        return;
    }

    unsigned pages = rd32(tab_node, 8u), page_sel = 0u;
    uint32_t page_node = 0u;
    if (pages < 1u || pages > MTF_MAX_PAGES ||
        selected_index(tab_node, &page_sel) != 1 || page_sel < 1u || page_sel > pages ||
        indexed_child(tab_node, page_sel, &page_node) != 1 || !is_valid_ptr(page_node) ||
        rd32(page_node, 0u) != expected_node) {
        s_mtf.last_err = 116u;
        return;
    }

    unsigned rows = rd32(page_node, 8u), row_sel = 0u;
    if (rows > MTF_MAX_ROWS) {
        s_mtf.last_err = 117u;
        return;
    }
    if (selected_index(page_node, &row_sel) != 1 || row_sel < 1u || row_sel > rows) {
        row_sel = 0u;
    }
    if (row_sel > 0u && rd32(menu, 0x1F8u) != row_sel - 1u) {
        wr32(menu, 0x1F8u, row_sel - 1u);
    }

    /* 2. 左右滑动翻页 */
    if (swipe == ZVP_MTF_GESTURE_SWIPE_LEFT || swipe == ZVP_MTF_GESTURE_SWIPE_RIGHT) {
        if (swipe == ZVP_MTF_GESTURE_SWIPE_LEFT) {
            if (page_sel < pages) {
                (void)set_selected_index(tab_node, page_sel + 1u);
            } else {
                (void)set_selected_index(root, (tab_sel < tabs) ? (tab_sel + 1u) : 1u);
            }
        } else {
            if (page_sel > 1u) {
                (void)set_selected_index(tab_node, page_sel - 1u);
            } else {
                (void)set_selected_index(root, (tab_sel > 1u) ? (tab_sel - 1u) : tabs);
            }
        }
        sync_refresh_menu(menu);
        s_mtf.last_action = ZVP_MTF_ACT_SWIPE_PAGE;
        s_mtf.events_handled++;
        return;
    }

    /* 3. 页码点点击跳转 */
    for (unsigned p = 0u; p < pages; p++) {
        if (is_hit(menu + 0x166B4u + p * 0x220u, &pt)) {
            if (p + 1u != page_sel && set_selected_index(tab_node, p + 1u) == 1) {
                sync_refresh_menu(menu);
            }
            s_mtf.last_action = ZVP_MTF_ACT_PAGE;
            s_mtf.events_handled++;
            return;
        }
    }

    /* 4. 菜单行点选（未选行=高亮，已高亮行=打开二级或触发设置） */
    for (unsigned r = 0u; r < rows; r++) {
        if (is_hit(menu + 0x9350u + r * 0x1118u, &pt)) {
            if (r + 1u != row_sel) {
                if (set_selected_index(page_node, r + 1u) == 1) {
                    wr32(menu, 0x1F8u, r);
                    wr32(menu, 0x1FCu, r);
                    wr8(menu, 0x1D8u, 0u);
                    if (sm_view) {
                        wr32(sm_view, 0x164u, 2u);
                        wr32(sm_view, 0x174u, r);
                    }
                    sync_refresh_menu(menu);
                }
                s_mtf.last_action = ZVP_MTF_ACT_ROW_SELECT;
                s_mtf.events_handled++;
            } else {
                uint32_t leaf = 0u;
                int is_popup = 0;
                if (indexed_child(page_node, r + 1u, &leaf) == 1 && is_valid_ptr(leaf)) {
                    int (*get_selectable)(uint32_t, int *) =
                        (int (*)(uint32_t, int *))(uintptr_t)rd32(rd32(leaf, 0u), 0x10u);
                    int sel_count = 0;
                    if (get_selectable && get_selectable(leaf, &sel_count) == 1 && sel_count > 0) {
                        is_popup = 1;
                    }
                }
                (void)set_selected_index(page_node, r + 1u);
                wr32(menu, 0x1F8u, r);
                wr32(menu, 0x1FCu, r);
                wr8(menu, 0x1D8u, 0u);
                if (sm_view) {
                    wr32(sm_view, 0x164u, 2u);
                    wr32(sm_view, 0x174u, r);
                }
                if (is_popup) {
                    (void)enter_selected(menu, leaf);
                } else if (sm_view) {
                    void (*action)(uint32_t, int) =
                        (void (*)(uint32_t, int))(uintptr_t)(view + MTF_OFF_ACTION_DISP);
                    action(sm_view, 11);
                } else {
                    push_key_click(MTF_EVT_KEY_ENTER);
                }
                s_mtf.last_action = ZVP_MTF_ACT_ROW_ENTER;
                s_mtf.events_handled++;
            }
            return;
        }
    }

    s_mtf.last_err = 199u;
}

static uint32_t find_fn_menu_view(void)
{
    if (!s_mtf.view4_base && s_mtf.ops.find_module_base) {
        s_mtf.view4_base = (uint32_t)s_mtf.ops.find_module_base("viewUnified4.so");
    }
    uint32_t v4_base = s_mtf.view4_base;
    if (!v4_base) {
        return 0u;
    }
    uint32_t vtv = v4_base + MTF_OFF_VTV_VIEW_FN;
    uint32_t sec_vtv = v4_base + MTF_OFF_VTV_VIEW_FN_S;

    uint32_t cached = s_mtf.cached_fn_view;
    if (is_valid_ptr(cached) && rd32(cached, 0u) == vtv && rd32(cached, 0x28u) == sec_vtv) {
        return cached;
    }
    s_mtf.cached_fn_view = 0u;

    /* 1. 先从 ViewManager 已注册视图数组安全检索 */
    uint32_t vm = get_view_mgr();
    if (vm) {
        int cnt = (int)rd32(vm, 0u);
        if (cnt > 0 && cnt <= 128) {
            uint32_t views_arr = rd32(vm, 4u);
            if (is_valid_ptr(views_arr)) {
                for (int i = 0; i < cnt; i++) {
                    uint32_t vwrap = rd32(views_arr, (uint32_t)i * 4u);
                    if (is_valid_ptr(vwrap)) {
                        if (rd32(vwrap, 0u) == vtv && rd32(vwrap, 0x28u) == sec_vtv) {
                            s_mtf.cached_fn_view = vwrap;
                            return vwrap;
                        }
                        uint32_t inner = rd32(vwrap, 0x18u);
                        if (is_valid_ptr(inner) && rd32(inner, 0u) == vtv && rd32(inner, 0x28u) == sec_vtv) {
                            s_mtf.cached_fn_view = inner;
                            return inner;
                        }
                    }
                }
            }
        }
    }

    /* 2. 兜底仅在 /proc/self/maps 确认的已映射 [heap] 范围内降序扫描，绝不碰空洞页 */
    uint32_t h_start = 0u, h_end = 0u;
    if (find_heap_range(&h_start, &h_end) && h_end > h_start + 0x200u) {
        uint32_t top = (h_end - 0x400u) & ~3u;
        for (uint32_t p = top; p >= h_start + 4u; p -= 4u) {
            if (rd32(p, 0u) == vtv && rd32(p, 0x28u) == sec_vtv) {
                uint32_t idx = rd32(p, 0x17Cu);
                if (idx <= 11u) {
                    s_mtf.cached_fn_view = p;
                    return p;
                }
            }
        }
    }
    return 0u;
}

static void step_fn_action(uint32_t view_fn, int action, void (*fn_action)(uint32_t, int))
{
    wr8(view_fn, 0x221u, 1u);
    wr8(view_fn, 0x3B4u, 0u);
    wr8(view_fn, 0x230u, 0u);
    wr8(view_fn, 0x231u, 0u);
    fn_action(view_fn, action);
}

static void handle_fn_touch(void *system_ptr, uint32_t mouse)
{
    refresh_dynamic_bases();
    uint32_t lib = s_mtf.lib_base;
    if (!lib) {
        return;
    }

    int16_t x = 0, y = 0;
    uint32_t sys_addr = system_ptr ? (uint32_t)(uintptr_t)system_ptr : (lib + MTF_OFF_SYSTEM_ADDR);
    void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) =
        (void (*)(uint32_t, unsigned, int16_t *, int16_t *))(uintptr_t)(lib + MTF_OFF_GET_POS);
    get_pos(sys_addr, mouse, &x, &y);

    if (x == 32767 || y == 32767) {
        s_mtf.last_err = 99u;
        s_mtf.press_valid = false;
        return;
    }

    if (s_mtf.press_valid) {
        int dx = (int)x - (int)s_mtf.press_x;
        int dy = (int)y - (int)s_mtf.press_y;
        if (dx >= -25 && dx <= 25 && dy >= -25 && dy <= 25) {
            x = s_mtf.press_x;
            y = s_mtf.press_y;
        }
    }
    s_mtf.press_valid = false;

    int tile = zvp_menu_touch_fn_tile_index(x, y);
    if (tile < 0) {
        push_key_click(MTF_EVT_KEY_FN);
        s_mtf.last_action = ZVP_MTF_ACT_FN_CANCEL;
        s_mtf.events_handled++;
        return;
    }

    uint32_t view_fn = find_fn_menu_view();
    if (!view_fn || !s_mtf.view4_base) {
        s_mtf.last_err = 301u;
        return;
    }

    /* 防 ISO/参数连续滑块子界面崩溃守卫：
     * viewUnified4.so @ 0x231bf8 实证：仅当 [view_fn+0x398] 为 2 或 3 时处于 2x6 瓷砖菜单；
     * 打开子菜单/滑块后置为 1，此时不强行向后台 ViewFnMenu 发送焦点或 Decide 动作。 */
    uint32_t fn_state = rd32(view_fn, 0x398u);
    if (fn_state != 2u && fn_state != 3u) {
        s_mtf.last_err = 302u;
        return;
    }

    uint32_t target_idx = (uint32_t)tile;
    uint32_t cur_idx = rd32(view_fn, 0x17Cu);
    if (cur_idx > 11u) {
        cur_idx = 0u;
    }

    uint32_t v4 = s_mtf.view4_base;
    void (*fn_action)(uint32_t, int) =
        (void (*)(uint32_t, int))(uintptr_t)(v4 + MTF_OFF_FN_ACTION);

    uint8_t focus_active = rd8(view_fn, 0x221u);
    if (focus_active && cur_idx == target_idx) {
        step_fn_action(view_fn, 4, fn_action); /* DECIDE */
        s_mtf.last_action = ZVP_MTF_ACT_FN_ENTER;
        s_mtf.events_handled++;
        return;
    }

    if (!focus_active) {
        wr8(view_fn, 0x221u, 1u);
        wr8(view_fn, 0x3B4u, 0u);
        void (*show_cursor)(uint32_t) =
            (void (*)(uint32_t))(uintptr_t)(v4 + MTF_OFF_FN_SHOW_CURSOR);
        void (*update_visual)(uint32_t) =
            (void (*)(uint32_t))(uintptr_t)(v4 + MTF_OFF_FN_UPDATE_TILE);
        show_cursor(view_fn);
        update_visual(view_fn);
    }

    int dir_act = 1;
    int steps = zvp_menu_touch_fn_ring_steps(cur_idx, target_idx, &dir_act);
    for (int k = 0; k < steps; k++) {
        step_fn_action(view_fn, dir_act, fn_action);
    }
    s_mtf.last_action = ZVP_MTF_ACT_FN_SELECT;
    s_mtf.events_handled++;
}

__attribute__((visibility("hidden")))
uint32_t zvp_mtf_hook_press(void *system, void *message)
{
    if (!s_mtf.hooks_installed) {
        return zvp_mtf_orig_press(system, message);
    }
    __sync_fetch_and_add(&s_mtf.inflight, 1u);
    uint32_t result = zvp_mtf_orig_press(system, message);
    bool active = s_mtf.enabled &&
                  (!s_mtf.ops.should_handle_fn || s_mtf.ops.should_handle_fn());
    if (active && message && s_mtf.lib_base) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            int16_t px = 0, py = 0;
            uint32_t sys_addr = system ? (uint32_t)(uintptr_t)system : (s_mtf.lib_base + MTF_OFF_SYSTEM_ADDR);
            void (*get_pos)(uint32_t, unsigned, int16_t *, int16_t *) =
                (void (*)(uint32_t, unsigned, int16_t *, int16_t *))(uintptr_t)(s_mtf.lib_base + MTF_OFF_GET_POS);
            get_pos(sys_addr, mouse, &px, &py);
            if (px != 32767 && py != 32767) {
                s_mtf.press_x = px;
                s_mtf.press_y = py;
                s_mtf.press_valid = true;
            }
        }
    }
    __sync_fetch_and_sub(&s_mtf.inflight, 1u);
    return result;
}

__attribute__((visibility("hidden")))
uint32_t zvp_mtf_hook_release(void *system, void *message)
{
    if (!s_mtf.hooks_installed) {
        return zvp_mtf_orig_release(system, message);
    }
    __sync_fetch_and_add(&s_mtf.inflight, 1u);
    uint32_t result = zvp_mtf_orig_release(system, message);
    bool active = s_mtf.enabled &&
                  (!s_mtf.ops.should_handle_fn || s_mtf.ops.should_handle_fn());
    if (active && message && s_mtf.lib_base && s_mtf.view2_base) {
        unsigned mouse = ((const uint8_t *)message)[0x14];
        if (mouse < 4u) {
            int (*get_fn_active)(void) =
                (int (*)(void))(uintptr_t)(s_mtf.view2_base + MTF_OFF_FN_EXISTENCE);
            if (get_fn_active && get_fn_active()) {
                handle_fn_touch(system, mouse);
            } else {
                handle_menu_touch(system, mouse);
            }
        }
    }
    __sync_fetch_and_sub(&s_mtf.inflight, 1u);
    return result;
}

static void encode_thumb_abs_branch(uint8_t out[12], uintptr_t target)
{
    /* ldr.w pc, [pc, #0]; .word target; nop; nop */
    uint32_t tgt = (uint32_t)target | 1u;
    out[0] = 0xDFu; out[1] = 0xF8u; out[2] = 0x00u; out[3] = 0xF0u;
    memcpy(out + 4, &tgt, 4u);
    out[8] = 0x00u; out[9] = 0xBFu; out[10] = 0x00u; out[11] = 0xBFu;
}

int zvp_menu_touch_init(const zvp_menu_touch_ops_t *ops)
{
    if (s_mtf.initialized) {
        return s_mtf.hooks_installed ? 0 : -1;
    }
    if (!ops || !ops->find_module_base || !ops->read_mem || !ops->write_mem) {
        return -1;
    }

    memset(&s_mtf, 0, sizeof(s_mtf));
    s_mtf.ops = *ops;
    s_mtf.enabled = true;
    s_mtf.initialized = true;

    refresh_dynamic_bases();
    if (!s_mtf.lib_base || !s_mtf.view2_base || !s_mtf.caution_base) {
        zvp_logf("[mtf] 跳过安装: 关键库基址缺失 (libObj=0x%x view2=0x%x caution=0x%x)",
                 (unsigned)s_mtf.lib_base, (unsigned)s_mtf.view2_base, (unsigned)s_mtf.caution_base);
        return -1;
    }

    uintptr_t rel_addr = (uintptr_t)(s_mtf.lib_base + MTF_OFF_HOOK_REL);
    uintptr_t prs_addr = (uintptr_t)(s_mtf.lib_base + MTF_OFF_HOOK_PRS);

    uint8_t probe_rel[12], probe_prs[12];
    if (s_mtf.ops.read_mem(rel_addr, probe_rel, sizeof(probe_rel)) != 0 ||
        s_mtf.ops.read_mem(prs_addr, probe_prs, sizeof(probe_prs)) != 0) {
        zvp_logf("[mtf] 跳过安装: 读取 libObj 序言失败");
        return -1;
    }

    /* Fail-Closed 校验：12 字节序言不匹配绝对拒绝写入 */
    if (memcmp(probe_rel, k_orig_rel, sizeof(k_orig_rel)) != 0 ||
        memcmp(probe_prs, k_orig_prs, sizeof(k_orig_prs)) != 0) {
        zvp_logf("[mtf] 序言校验失配，安全跳过安装 (rel[0..3]=%02x%02x%02x%02x prs[0..3]=%02x%02x%02x%02x)",
                 probe_rel[0], probe_rel[1], probe_rel[2], probe_rel[3],
                 probe_prs[0], probe_prs[1], probe_prs[2], probe_prs[3]);
        return -1;
    }

    uint32_t rel_resume = (uint32_t)(rel_addr + 12u) | 1u;
    uint32_t prs_resume = (uint32_t)(prs_addr + 12u) | 1u;
    if (s_mtf.ops.write_mem((uintptr_t)&zvp_mtf_rel_resume_slot, &rel_resume, sizeof(rel_resume)) != 0 ||
        s_mtf.ops.write_mem((uintptr_t)&zvp_mtf_prs_resume_slot, &prs_resume, sizeof(prs_resume)) != 0) {
        zvp_logf("[mtf] 跳板复原槽写入失败");
        return -1;
    }

    uint8_t br_rel[12], br_prs[12];
    encode_thumb_abs_branch(br_rel, (uintptr_t)&zvp_mtf_hook_release);
    encode_thumb_abs_branch(br_prs, (uintptr_t)&zvp_mtf_hook_press);

    if (s_mtf.ops.write_mem(rel_addr, br_rel, sizeof(br_rel)) != 0 ||
        s_mtf.ops.write_mem(prs_addr, br_prs, sizeof(br_prs)) != 0) {
        /* 如仅写了一半则立刻回滚 */
        (void)s_mtf.ops.write_mem(rel_addr, k_orig_rel, sizeof(k_orig_rel));
        (void)s_mtf.ops.write_mem(prs_addr, k_orig_prs, sizeof(k_orig_prs));
        zvp_logf("[mtf] 内联分支补丁写入失败，已回滚");
        return -1;
    }

    __sync_synchronize();
    s_mtf.hooks_installed = true;
    zvp_logf("[mtf] 原厂菜单/Fn触控激活成功: libObj=0x%x view2=0x%x caution=0x%x view4=0x%x",
             (unsigned)s_mtf.lib_base, (unsigned)s_mtf.view2_base,
             (unsigned)s_mtf.caution_base, (unsigned)s_mtf.view4_base);
    return 0;
}

void zvp_menu_touch_set_enabled(bool enabled)
{
    s_mtf.enabled = enabled;
}

bool zvp_menu_touch_is_enabled(void)
{
    return s_mtf.enabled;
}

void zvp_menu_touch_uninstall(void)
{
    s_mtf.enabled = false;
    if (!s_mtf.hooks_installed || !s_mtf.lib_base || !s_mtf.ops.write_mem) {
        return;
    }
    uintptr_t rel_addr = (uintptr_t)(s_mtf.lib_base + MTF_OFF_HOOK_REL);
    uintptr_t prs_addr = (uintptr_t)(s_mtf.lib_base + MTF_OFF_HOOK_PRS);
    s_mtf.hooks_installed = false;
    __sync_synchronize();
    (void)s_mtf.ops.write_mem(rel_addr, k_orig_rel, sizeof(k_orig_rel));
    (void)s_mtf.ops.write_mem(prs_addr, k_orig_prs, sizeof(k_orig_prs));
    zvp_logf("[mtf] 已安全卸载原厂菜单/Fn触控内联钩子");
}

void zvp_menu_touch_get_stats(zvp_menu_touch_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    out_stats->hooks_installed = s_mtf.hooks_installed;
    out_stats->enabled = s_mtf.enabled;
    out_stats->last_action = s_mtf.last_action;
    out_stats->last_err = s_mtf.last_err;
    out_stats->events_handled = s_mtf.events_handled;
    out_stats->libobj_base = (uintptr_t)s_mtf.lib_base;
    out_stats->view2_base = (uintptr_t)s_mtf.view2_base;
    out_stats->view4_base = (uintptr_t)s_mtf.view4_base;
    out_stats->caution_base = (uintptr_t)s_mtf.caution_base;
}

#else

static bool s_host_mtf_enabled = true;

int zvp_menu_touch_init(const zvp_menu_touch_ops_t *ops)
{
    (void)ops;
    return 0;
}

void zvp_menu_touch_set_enabled(bool enabled)
{
    s_host_mtf_enabled = enabled;
}

bool zvp_menu_touch_is_enabled(void)
{
    return s_host_mtf_enabled;
}

void zvp_menu_touch_uninstall(void)
{
    s_host_mtf_enabled = false;
}

void zvp_menu_touch_get_stats(zvp_menu_touch_stats_t *out_stats)
{
    if (!out_stats) return;
    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->enabled = s_host_mtf_enabled;
}

#endif
