#include "gui.h"
#include "settings_manager.h"
#include <Arduino.h>
#include <stdio.h>

// ---- Touch UI state: swipe paging -----------------------------------------
// Pages are built dynamically once the Beszel systems list is known: one
// dashboard page per system, then a single combined container page. The arrays
// are sized for the max systems plus that container page.
#define MAX_PAGES (BESZEL_MAX_SYSTEMS + 1)

static lv_obj_t *pages[MAX_PAGES] = {NULL};
static lv_obj_t *page_dots[MAX_PAGES] = {NULL};
static lv_obj_t *dots_cont = NULL;
static lv_obj_t *placeholder = NULL;
static int page_count = 0;
static int current_page = 0;

#define METRIC_GLYPH_SLOTS 8

struct GlyphValue
{
    lv_obj_t *slot[METRIC_GLYPH_SLOTS];
    uint8_t count;
};

struct ArcParts
{
    lv_obj_t *title;
    GlyphValue value;
    lv_obj_t *suffix;
    lv_obj_t *info;
};

struct CompactParts
{
    lv_obj_t *icon;
    lv_obj_t *prefix;
    GlyphValue value;
    lv_obj_t *suffix;
    bool temperature;
    int8_t temperature_band;
};

static GlyphValue create_glyph_value(lv_obj_t *parent, uint8_t count,
                                     const lv_font_t *font, lv_color_t color,
                                     lv_coord_t cell_width);
static void set_glyph_value(GlyphValue &value, const char *text, bool animate = true);
static void set_metric_text(lv_obj_t *label, const char *text);

static void free_widget_parts(lv_event_t *event)
{
    lv_mem_free(lv_event_get_user_data(event));
}

// Per-system dashboard widget handles, filled by build_dashboard_page() and
// refreshed by gui_update_dashboard().
struct DashWidgets
{
    lv_obj_t *page;
    lv_obj_t *name_label;
    ArcWithLabel cpu_arc;
    ArcWithLabel ram_arc;
    lv_obj_t *temp_label;
    lv_obj_t *gpu_label;
    lv_obj_t *uptime_label;
    lv_obj_t *disk_label;
    lv_obj_t *net_label;
    lv_obj_t *vram_label;
    bool has_status;
    bool last_up;
};
static DashWidgets dash[BESZEL_MAX_SYSTEMS];
static int dash_count = 0;

// Combined container page (populated by beszel_api.cpp).
lv_obj_t *container_label = NULL;
lv_obj_t *container_header = NULL;

ArcWithLabel create_arc(lv_obj_t *parent, const char *text, lv_color_t color)
{
    ArcWithLabel result = {nullptr, nullptr};
    if (!parent)
        return result;

    lv_obj_t *arc = lv_arc_create(parent);
    if (!arc)
        return result;

    lv_obj_set_size(arc, 110, 110);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 0);

    lv_obj_set_style_arc_color(arc, lv_color_darken(color, LV_OPA_30), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *cont = lv_obj_create(arc);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 90, 90);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_center(cont);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 2, 0);

    lv_obj_t *title = lv_label_create(cont);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, color, 0);
    lv_label_set_text(title, text);

    lv_obj_t *value_row = lv_obj_create(cont);
    lv_obj_remove_style_all(value_row);
    lv_obj_set_size(value_row, 70, 20);
    lv_obj_set_style_translate_x(value_row, -5, 0);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(value_row, 0, 0);

    ArcParts *parts = (ArcParts *)lv_mem_alloc(sizeof(ArcParts));
    memset(parts, 0, sizeof(ArcParts));
    parts->title = title;
    parts->value = create_glyph_value(value_row, 5, &lv_font_montserrat_16, color, 11);
    set_glyph_value(parts->value, "--", false);

    parts->suffix = lv_label_create(value_row);
    lv_obj_set_width(parts->suffix, 12);
    lv_obj_set_style_text_align(parts->suffix, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(parts->suffix, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(parts->suffix, color, 0);
    lv_label_set_text(parts->suffix, "%");

    lv_obj_t *info = lv_label_create(cont);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x808080), 0);
    lv_label_set_text(info, "--");
    parts->info = info;
    lv_obj_set_user_data(arc, parts);
    lv_obj_add_event_cb(arc, free_widget_parts, LV_EVENT_DELETE, parts);

    result.arc = arc;
    result.label = cont;
    return result;
}

lv_obj_t *create_button_label(lv_obj_t *parent, const char *text, const ThemeColors *theme)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 145, 30);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_bg_color(btn, theme->card_bg_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme->border_color, 0);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_darken(theme->bg_color, LV_OPA_30), 0);
    lv_obj_set_style_pad_all(btn, 5, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, theme->text_color, 0);

    return label;
}

static GlyphValue create_glyph_value(lv_obj_t *parent, uint8_t count,
                                     const lv_font_t *font, lv_color_t color,
                                     lv_coord_t cell_width)
{
    GlyphValue value = {};
    value.count = count > METRIC_GLYPH_SLOTS ? METRIC_GLYPH_SLOTS : count;
    for (uint8_t i = 0; i < value.count; i++)
    {
        value.slot[i] = lv_label_create(parent);
        lv_obj_set_width(value.slot[i], cell_width);
        lv_obj_set_style_text_align(value.slot[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(value.slot[i], font, 0);
        lv_obj_set_style_text_color(value.slot[i], color, 0);
        lv_label_set_text(value.slot[i], " ");
    }
    return value;
}

static lv_obj_t *create_compact_metric(lv_obj_t *parent, const char *icon,
                                       const char *prefix, const char *initial,
                                       const char *suffix, uint8_t slots,
                                       const ThemeColors *theme,
                                       bool temperature = false)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 154, 30);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_bg_color(btn, theme->card_bg_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme->border_color, 0);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_darken(theme->bg_color, LV_OPA_30), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *icon_label = lv_label_create(btn);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_width(icon_label, 20);
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_label, theme->text_color, 0);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 5, 0);

    CompactParts *parts = (CompactParts *)lv_mem_alloc(sizeof(CompactParts));
    memset(parts, 0, sizeof(CompactParts));
    parts->icon = icon_label;
    parts->temperature = temperature;
    parts->temperature_band = -1;

    parts->prefix = lv_label_create(btn);
    lv_obj_set_width(parts->prefix, 48);
    lv_label_set_long_mode(parts->prefix, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(parts->prefix, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(parts->prefix, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(parts->prefix, theme->text_color, 0);
    lv_label_set_text(parts->prefix, prefix);
    lv_obj_align(parts->prefix, LV_ALIGN_LEFT_MID, 30, 0);

    lv_obj_t *value_row = lv_obj_create(btn);
    lv_obj_remove_style_all(value_row);
    lv_obj_set_size(value_row, slots * 8, 18);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(value_row, 0, 0);
    const lv_coord_t value_right = prefix[0] ? 120 : 140;
    lv_obj_align(value_row, LV_ALIGN_LEFT_MID, value_right - slots * 8, 0);
    parts->value = create_glyph_value(value_row, slots, &lv_font_montserrat_14,
                                      theme->text_color, 8);
    set_glyph_value(parts->value, initial, false);

    parts->suffix = lv_label_create(btn);
    lv_obj_set_width(parts->suffix, 14);
    lv_label_set_long_mode(parts->suffix, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(parts->suffix, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(parts->suffix, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(parts->suffix, theme->text_color, 0);
    lv_label_set_text(parts->suffix, suffix);
    lv_obj_align(parts->suffix, LV_ALIGN_LEFT_MID, 123, 0);

    lv_obj_set_user_data(btn, parts);
    lv_obj_add_event_cb(btn, free_widget_parts, LV_EVENT_DELETE, parts);

    return btn;
}

static void animate_metric_opa(void *label, int32_t opa)
{
    lv_obj_set_style_text_opa((lv_obj_t *)label, (lv_opa_t)opa, 0);
}

static void delete_transition_label(lv_anim_t *animation)
{
    lv_obj_t *label = (lv_obj_t *)animation->var;
    if (label && lv_obj_is_valid(label))
        lv_obj_del_async(label);
}

static void set_metric_text(lv_obj_t *label, const char *text)
{
    if (!label || !text || strcmp(lv_label_get_text(label), text) == 0)
        return;

    lv_anim_del(label, animate_metric_opa);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_label_set_text(label, text);
}

static void transition_glyph(lv_obj_t *label, const char *text)
{
    if (!label || !text || strcmp(lv_label_get_text(label), text) == 0)
        return;

    lv_obj_update_layout(label);
    lv_obj_t *outgoing = lv_label_create(lv_obj_get_parent(label));
    lv_obj_add_flag(outgoing, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(outgoing, lv_obj_get_x(label), lv_obj_get_y(label));
    lv_obj_set_size(outgoing, lv_obj_get_width(label), lv_obj_get_height(label));
    lv_obj_set_style_text_align(
        outgoing, lv_obj_get_style_text_align(label, LV_PART_MAIN), 0);
    lv_obj_set_style_text_font(
        outgoing, lv_obj_get_style_text_font(label, LV_PART_MAIN), 0);
    lv_obj_set_style_text_color(
        outgoing, lv_obj_get_style_text_color(label, LV_PART_MAIN), 0);
    lv_obj_set_style_text_opa(outgoing, LV_OPA_COVER, 0);
    lv_label_set_text(outgoing, lv_label_get_text(label));

    lv_anim_del(label, animate_metric_opa);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_opa(label, LV_OPA_TRANSP, 0);

    lv_anim_t incoming;
    lv_anim_init(&incoming);
    lv_anim_set_var(&incoming, label);
    lv_anim_set_exec_cb(&incoming, animate_metric_opa);
    lv_anim_set_values(&incoming, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&incoming, 320);
    lv_anim_set_path_cb(&incoming, lv_anim_path_linear);
    lv_anim_start(&incoming);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, outgoing);
    lv_anim_set_exec_cb(&fade, animate_metric_opa);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fade, 320);
    lv_anim_set_path_cb(&fade, lv_anim_path_linear);
    lv_anim_set_ready_cb(&fade, delete_transition_label);
    lv_anim_start(&fade);
}

static void set_glyph_value(GlyphValue &value, const char *text, bool animate)
{
    if (!text || value.count == 0)
        return;
    const size_t text_len = strlen(text);
    const size_t visible_len = text_len > value.count ? value.count : text_len;
    const size_t source_start = text_len - visible_len;
    const size_t padding = value.count - visible_len;
    for (uint8_t i = 0; i < value.count; i++)
    {
        char glyph[2] = {' ', '\0'};
        if (i >= padding)
            glyph[0] = text[source_start + i - padding];
        if (animate)
            transition_glyph(value.slot[i], glyph);
        else
            set_metric_text(value.slot[i], glyph);
    }
}

static void update_compact_metric(lv_obj_t *btn, const char *value,
                                  const char *suffix = nullptr,
                                  int temperature = -1000)
{
    if (!btn)
        return;
    CompactParts *parts = (CompactParts *)lv_obj_get_user_data(btn);
    if (!parts)
        return;
    if (parts->temperature && temperature != -1000)
    {
        int8_t band = temperature < 40 ? 0 : (temperature < 50 ? 1 : 2);
        if (band != parts->temperature_band)
        {
            lv_color_t color = band == 0 ? lv_color_hex(0x00FF44)
                                         : (band == 1 ? lv_color_hex(0xFFAA00)
                                                      : lv_color_hex(0xFF4444));
            for (uint8_t i = 0; i < parts->value.count; i++)
                lv_obj_set_style_text_color(parts->value.slot[i], color, 0);
            parts->temperature_band = band;
        }
    }
    set_glyph_value(parts->value, value);
    if (suffix)
        set_metric_text(parts->suffix, suffix);
}

void set_arc_value_animated(lv_obj_t *arc, int32_t value, uint32_t duration)
{
    if (!arc)
        return;

    value = (value < 0) ? 0 : (value > 100) ? 100
                                            : value;
    if (lv_arc_get_value(arc) == value)
        return;

    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_values(&a, lv_arc_get_value(arc), value);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, NULL);
    lv_anim_start(&a);
}

void update_arc_label(lv_obj_t *label, const char *text)
{
    if (!label || !text)
        return;
    set_metric_text(label, text);
}

static void theme_arc(ArcWithLabel &a, lv_color_t color, const ThemeColors &theme)
{
    if (!a.arc)
        return;
    lv_obj_set_style_arc_color(a.arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a.arc, lv_color_darken(color, LV_OPA_30), LV_PART_MAIN);
    ArcParts *parts = (ArcParts *)lv_obj_get_user_data(a.arc);
    if (parts)
    {
        lv_obj_set_style_text_color(parts->title, theme.text_color, 0);
        for (uint8_t i = 0; i < parts->value.count; i++)
            lv_obj_set_style_text_color(parts->value.slot[i], theme.text_color, 0);
        lv_obj_set_style_text_color(parts->suffix, theme.text_color, 0);
        lv_obj_set_style_text_color(parts->info, lv_color_hex(0x808080), 0);
    }
}

static void theme_compact(lv_obj_t *label, const ThemeColors &theme)
{
    if (!label)
        return;
    lv_obj_set_style_border_color(label, theme.border_color, 0);
    lv_obj_set_style_bg_color(label, theme.card_bg_color, 0);
    CompactParts *parts = (CompactParts *)lv_obj_get_user_data(label);
    if (!parts)
        return;
    lv_obj_set_style_text_color(parts->icon, theme.text_color, 0);
    lv_obj_set_style_text_color(parts->prefix, theme.text_color, 0);
    lv_obj_set_style_text_color(parts->suffix, theme.text_color, 0);
    if (!parts->temperature || parts->temperature_band < 0)
        for (uint8_t i = 0; i < parts->value.count; i++)
            lv_obj_set_style_text_color(parts->value.slot[i], theme.text_color, 0);
}

void applyTheme(bool darkMode)
{
    const ThemeColors &theme = SettingsManager::getCurrentTheme();
    lv_obj_set_style_bg_color(lv_scr_act(), theme.bg_color, 0);

    for (int i = 0; i < dash_count; i++)
    {
        DashWidgets &d = dash[i];
        theme_arc(d.cpu_arc, theme.cpu_color, theme);
        theme_arc(d.ram_arc, theme.ram_color, theme);
        if (d.name_label)
            lv_obj_set_style_text_color(d.name_label, theme.cpu_color, 0);
        theme_compact(d.temp_label, theme);
        theme_compact(d.gpu_label, theme);
        theme_compact(d.uptime_label, theme);
        theme_compact(d.disk_label, theme);
        theme_compact(d.net_label, theme);
        theme_compact(d.vram_label, theme);
    }

    if (container_label)
        lv_obj_set_style_text_color(container_label, theme.text_color, 0);
}

// ---- Swipe paging ---------------------------------------------------------

static void update_dots()
{
    for (int i = 0; i < page_count; i++)
    {
        if (!page_dots[i])
            continue;
        bool active = (i == current_page);
        lv_obj_set_style_bg_opa(page_dots[i], active ? LV_OPA_COVER : LV_OPA_20, 0);
        int sz = active ? 9 : 5;
        lv_obj_set_size(page_dots[i], sz, sz);
        lv_obj_set_style_radius(page_dots[i], sz / 2, 0);
    }
}

bool gui_container_page_active()
{
    return page_count > 0 && current_page == page_count - 1;
}

int gui_active_system_index()
{
    return current_page >= 0 && current_page < dash_count ? current_page : -1;
}

void gui_container_scroll_by(int16_t dy)
{
    if (!container_label)
        return;
    lv_obj_t *view = lv_obj_get_parent(container_label);
    if (!view)
        return;
    // Clamp to the available scroll room so the list can't be dragged past its
    // ends. Positive dy moves content down (reveals the top); negative reveals
    // the bottom.
    if (dy > 0)
    {
        lv_coord_t room = lv_obj_get_scroll_top(view);
        if (dy > room)
            dy = room;
    }
    else if (dy < 0)
    {
        lv_coord_t room = lv_obj_get_scroll_bottom(view);
        if (-dy > room)
            dy = -room;
    }
    if (dy)
        lv_obj_scroll_by(view, 0, dy, LV_ANIM_OFF);
}

// ---- Inertial (momentum) scrolling for the container list -----------------
static lv_timer_t *fling_timer = NULL;
static float fling_vel = 0.0f; // px per tick, decays each tick

static void fling_timer_cb(lv_timer_t *t)
{
    if (!gui_container_page_active())
    {
        fling_vel = 0.0f;
        lv_timer_pause(t);
        return;
    }
    fling_vel *= 0.82f; // friction
    int16_t dy = (int16_t)fling_vel;
    if (dy == 0)
    {
        fling_vel = 0.0f;
        lv_timer_pause(t);
        return;
    }
    gui_container_scroll_by(dy);
}

void gui_container_fling(int16_t velocity)
{
    fling_vel = (float)velocity;
    if (fling_vel > -1.0f && fling_vel < 1.0f)
        return; // too slow to bother coasting
    if (!fling_timer)
        fling_timer = lv_timer_create(fling_timer_cb, 20, NULL);
    lv_timer_reset(fling_timer);
    lv_timer_resume(fling_timer);
}

void gui_container_fling_stop()
{
    fling_vel = 0.0f;
    if (fling_timer)
        lv_timer_pause(fling_timer);
}

static lv_timer_t *rotate_timer = NULL;

void gui_next_page()
{
    if (page_count == 0)
        return;
    current_page = (current_page + 1) % page_count;
    for (int i = 0; i < page_count; i++)
    {
        if (i == current_page)
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    update_dots();

    if (rotate_timer)
        lv_timer_reset(rotate_timer);
}

static void rotate_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    gui_next_page();
}

void gui_set_auto_rotate(bool enabled, uint32_t interval_sec)
{
    if (interval_sec < 1)
        interval_sec = 1;

    if (!rotate_timer)
        rotate_timer = lv_timer_create(rotate_timer_cb, interval_sec * 1000, NULL);
    else
        lv_timer_set_period(rotate_timer, interval_sec * 1000);

    lv_timer_reset(rotate_timer);
    if (enabled)
        lv_timer_resume(rotate_timer);
    else
        lv_timer_pause(rotate_timer);
}

// ---- Page construction ----------------------------------------------------

static lv_obj_t *create_page()
{
    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, 320, 240);
    lv_obj_center(page);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void build_dashboard_page(lv_obj_t *parent, const ThemeColors *theme,
                                 DashWidgets &d, const char *name)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Host name header, styled like the Containers page title.
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, name);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, theme->cpu_color, 0);
    lv_obj_set_style_pad_top(header, 8, 0);
    d.name_label = header;

    // Body: two columns of arc + metric cards.
    // Fills whatever height the header leaves, so a taller header never clips
    // the bottom cards.
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, 320);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_col = lv_obj_create(body);
    lv_obj_t *right_col = lv_obj_create(body);
    for (lv_obj_t *col : {left_col, right_col})
    {
        lv_obj_set_width(col, 158);
        lv_obj_set_height(col, lv_pct(100));
        lv_obj_set_style_pad_all(col, 2, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    }

    d.cpu_arc = create_arc(left_col, "CPU", theme->cpu_color);
    d.temp_label = create_compact_metric(left_col, LV_SYMBOL_WARNING, "Temp:", "--", "C", 4, theme, true);
    d.gpu_label = create_compact_metric(left_col, LV_SYMBOL_CHARGE, "GPU:", "--", "%", 4, theme);
    d.uptime_label = create_compact_metric(left_col, LV_SYMBOL_POWER, "", "---", "", 8, theme);

    d.ram_arc = create_arc(right_col, "RAM", theme->ram_color);
    d.disk_label = create_compact_metric(right_col, LV_SYMBOL_DRIVE, "Disk:", "--", "%", 4, theme);
    d.vram_label = create_compact_metric(right_col, LV_SYMBOL_SAVE, "VRAM:", "--", "%", 4, theme);
    d.net_label = create_compact_metric(right_col, LV_SYMBOL_SHUFFLE, "Net:", "---", "", 5, theme);

    d.page = parent;
}

static void build_container_page(lv_obj_t *parent, const ThemeColors *theme)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_row(parent, 4, 0);

    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, LV_SYMBOL_LIST " Containers");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, theme->cpu_color, 0);
    container_header = header;

    char hdr[40];
    snprintf(hdr, sizeof(hdr), CONTAINER_HDR_FMT, "Name", "CPU%", "MEM");
    lv_obj_t *colhead = lv_label_create(parent);
    lv_label_set_text(colhead, hdr);
    lv_obj_set_style_text_font(colhead, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(colhead, lv_color_hex(0x808080), 0);

    lv_obj_t *view = lv_obj_create(parent);
    lv_obj_remove_style_all(view);
    lv_obj_set_width(view, lv_pct(100));
    lv_obj_set_flex_grow(view, 1);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_set_scroll_dir(view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view, LV_SCROLLBAR_MODE_AUTO);
    // We scroll this view ourselves from the touch deltas (gui_container_scroll_by),
    // so disable LVGL's own pointer-scroll to avoid the two fighting.
    lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);

    container_label = lv_label_create(view);
    lv_label_set_long_mode(container_label, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(container_label, true);
    lv_obj_set_width(container_label, lv_pct(100));
    lv_obj_set_style_text_font(container_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(container_label, theme->text_color, 0);
    lv_label_set_text(container_label, "Loading containers...");
}

static void build_dots(const ThemeColors *theme)
{
    dots_cont = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(dots_cont);
    lv_obj_set_size(dots_cont, page_count * 16, 12);
    lv_obj_align(dots_cont, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots_cont, 8, 0);
    lv_obj_clear_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < page_count; i++)
    {
        lv_obj_t *dot = lv_obj_create(dots_cont);
        lv_obj_remove_style_all(dot);
        lv_obj_set_style_bg_color(dot, theme->cpu_color, 0);
        page_dots[i] = dot;
    }
    update_dots();
}

void create_system_monitor_gui()
{
    const ThemeColors *theme = DARK_MODE ? &dark_theme : &light_theme;
    lv_obj_set_style_bg_color(lv_scr_act(), theme->bg_color, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    // Until the first systems list arrives we just show a status line; the real
    // pages are built in gui_build_system_pages().
    placeholder = lv_label_create(lv_scr_act());
    lv_label_set_text(placeholder, "Connecting to Beszel...");
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(placeholder, theme->text_color, 0);
    lv_obj_center(placeholder);

    SettingsManager::setThemeChangeCallback(applyTheme);
}

void gui_build_system_pages(const BeszelSystem *systems, int count)
{
    const ThemeColors *theme = DARK_MODE ? &dark_theme : &light_theme;

    if (placeholder)
    {
        lv_obj_del(placeholder);
        placeholder = NULL;
    }

    // Tear down any previously built pages/dots (rebuild on system-set change).
    for (int i = 0; i < page_count; i++)
    {
        if (pages[i])
            lv_obj_del(pages[i]);
        pages[i] = NULL;
        page_dots[i] = NULL;
    }
    if (dots_cont)
    {
        lv_obj_del(dots_cont);
        dots_cont = NULL;
    }
    container_label = NULL;
    container_header = NULL;

    if (count > BESZEL_MAX_SYSTEMS)
        count = BESZEL_MAX_SYSTEMS;
    dash_count = count;

    // One dashboard page per system.
    for (int i = 0; i < count; i++)
    {
        memset(&dash[i], 0, sizeof(DashWidgets));
        pages[i] = create_page();
        build_dashboard_page(pages[i], theme, dash[i], systems[i].name);
    }

    // Combined container page last.
    pages[count] = create_page();
    build_container_page(pages[count], theme);

    page_count = count + 1;
    current_page = 0;
    for (int i = 0; i < page_count; i++)
    {
        if (i == 0)
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_dots(theme);
    applyTheme(SettingsManager::getDarkMode());

    // Seed the new dashboards with whatever we already have.
    for (int i = 0; i < count; i++)
        gui_update_dashboard(i, systems[i]);
}

// Keep the changing number separate from its unit so neither Net: nor the unit
// participates in the per-character numeric redraw.
static const char *format_rate(float bytes_per_sec, char *buffer, size_t len)
{
    if (bytes_per_sec >= 1024.0f * 1024.0f)
    {
        snprintf(buffer, len, "%.1f", bytes_per_sec / (1024.0f * 1024.0f));
        return "M";
    }
    else if (bytes_per_sec >= 1024.0f)
    {
        snprintf(buffer, len, "%.1f", bytes_per_sec / 1024.0f);
        return "K";
    }
    snprintf(buffer, len, "%.0f", bytes_per_sec);
    return "B";
}

static void format_uptime(unsigned long secs, char *buffer, size_t len)
{
    if (secs >= 86400UL)
        snprintf(buffer, len, "%lud %luh", secs / 86400UL, (secs % 86400UL) / 3600UL);
    else if (secs >= 3600UL)
        snprintf(buffer, len, "%luh %lum", secs / 3600UL, (secs % 3600UL) / 60UL);
    else
        snprintf(buffer, len, "%lum", secs / 60UL);
}

void gui_update_dashboard(int idx, const BeszelSystem &sys)
{
    if (idx < 0 || idx >= dash_count)
        return;
    DashWidgets &d = dash[idx];
    char buf[48];

    if (d.name_label)
    {
        if (strcmp(lv_label_get_text(d.name_label), sys.name) != 0)
            lv_label_set_text(d.name_label, sys.name);
        if (!d.has_status || d.last_up != sys.up)
        {
            // Cyan like the Containers title when up; red flags a down host.
            lv_obj_set_style_text_color(
                d.name_label,
                sys.up ? SettingsManager::getCurrentTheme().cpu_color : lv_color_hex(0xE0504F), 0);
            d.last_up = sys.up;
            d.has_status = true;
        }
    }

    // CPU arc: big % value, cores on the info line.
    if (d.cpu_arc.arc)
    {
        ArcParts *parts = (ArcParts *)lv_obj_get_user_data(d.cpu_arc.arc);
        if (parts)
        {
            snprintf(buf, sizeof(buf), "%.1f", sys.cpu);
            set_glyph_value(parts->value, buf);
            snprintf(buf, sizeof(buf), "%d cores", sys.cores);
            set_metric_text(parts->info, buf);
        }
        set_arc_value_animated(d.cpu_arc.arc, (int)sys.cpu);
    }

    // RAM arc: % value, with total GB (from system_stats) on the info line.
    if (d.ram_arc.arc)
    {
        ArcParts *parts = (ArcParts *)lv_obj_get_user_data(d.ram_arc.arc);
        if (parts)
        {
            snprintf(buf, sizeof(buf), "%.1f", sys.mem);
            set_glyph_value(parts->value, buf);
            if (sys.hasMemTotal)
                snprintf(buf, sizeof(buf), "/ %.1f GB", sys.memTotalGB);
            else
                buf[0] = '\0';
            set_metric_text(parts->info, buf);
        }
        set_arc_value_animated(d.ram_arc.arc, (int)sys.mem);
    }

    if (sys.hasTemp)
    {
        snprintf(buf, sizeof(buf), "%d", (int)sys.temp);
        update_compact_metric(d.temp_label, buf, nullptr, (int)sys.temp);
    }
    else
        update_compact_metric(d.temp_label, "--");

    if (sys.hasGpu)
    {
        snprintf(buf, sizeof(buf), "%d", (int)sys.gpu);
        update_compact_metric(d.gpu_label, buf);
    }
    else
        update_compact_metric(d.gpu_label, "--");

    char tmp[24];
    format_uptime(sys.uptime, tmp, sizeof(tmp));
    update_compact_metric(d.uptime_label, tmp);

    snprintf(buf, sizeof(buf), "%d", (int)sys.disk);
    update_compact_metric(d.disk_label, buf);

    if (sys.hasVram)
    {
        snprintf(buf, sizeof(buf), "%d", (int)sys.vram);
        update_compact_metric(d.vram_label, buf);
    }
    else
        update_compact_metric(d.vram_label, "--");

    const char *rate_unit = format_rate(sys.bw, tmp, sizeof(tmp));
    update_compact_metric(d.net_label, tmp, rate_unit);
}
