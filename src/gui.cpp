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

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(value, color, 0);
    lv_label_set_text(value, "--");

    lv_obj_t *info = lv_label_create(cont);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x808080), 0);
    lv_label_set_text(info, "--");

    lv_obj_t **labels = (lv_obj_t **)lv_mem_alloc(3 * sizeof(lv_obj_t *));
    labels[0] = title;
    labels[1] = value;
    labels[2] = info;
    lv_obj_set_user_data(arc, labels);

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

lv_obj_t *create_compact_label(lv_obj_t *parent, const char *text, const ThemeColors *theme)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 154, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_bg_color(btn, theme->card_bg_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, theme->border_color, 0);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_darken(theme->bg_color, LV_OPA_30), 0);
    lv_obj_set_style_pad_all(btn, 5, 0);

    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon_label = lv_label_create(btn);
    char icon[32];
    const char *space_pos = strchr(text, ' ');
    if (space_pos)
    {
        size_t icon_len = space_pos - text;
        strncpy(icon, text, icon_len);
        icon[icon_len] = '\0';
    }
    else
    {
        strcpy(icon, "");
    }
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_label, theme->text_color, 0);

    lv_obj_t *text_label = lv_label_create(btn);
    if (space_pos)
        lv_label_set_text(text_label, space_pos + 1);
    else
        lv_label_set_text(text_label, "");
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);
    if (!strstr(text, "Temp:"))
        lv_obj_set_style_text_color(text_label, theme->text_color, 0);
    lv_obj_set_user_data(btn, text_label);

    return btn;
}

void update_compact_label(lv_obj_t *btn, const char *text)
{
    if (!btn)
        return;
    lv_obj_t *text_label = (lv_obj_t *)lv_obj_get_user_data(btn);

    if (strstr(text, "Temp:"))
    {
        const char *temp_start = strstr(text, "Temp:") + 5;
        int temperature = 0;

        if (sscanf(temp_start, "%d", &temperature) == 1)
        {
            lv_color_t temp_color;
            if (temperature < 40)
                temp_color = lv_color_hex(0x00FF44);
            else if (temperature < 50)
                temp_color = lv_color_hex(0xFFAA00);
            else
                temp_color = lv_color_hex(0xFF4444);
            lv_obj_set_style_text_color(text_label, temp_color, 0);
        }

        lv_label_set_text(text_label, temp_start);
        return;
    }

    const char *space_pos = strchr(text, ' ');
    if (space_pos)
        lv_label_set_text(text_label, space_pos + 1);
}

void set_arc_value_animated(lv_obj_t *arc, int32_t value, uint32_t duration)
{
    if (!arc)
        return;

    value = (value < 0) ? 0 : (value > 100) ? 100
                                            : value;

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
    lv_label_set_text(label, text);
}

static void theme_arc(ArcWithLabel &a, lv_color_t color, const ThemeColors &theme)
{
    if (!a.arc)
        return;
    lv_obj_set_style_arc_color(a.arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a.arc, lv_color_darken(color, LV_OPA_30), LV_PART_MAIN);
    lv_obj_t **labels = (lv_obj_t **)lv_obj_get_user_data(a.arc);
    if (labels)
    {
        lv_obj_set_style_text_color(labels[0], theme.text_color, 0);
        lv_obj_set_style_text_color(labels[1], theme.text_color, 0);
        lv_obj_set_style_text_color(labels[2], lv_color_hex(0x808080), 0);
    }
}

static void theme_compact(lv_obj_t *label, const ThemeColors &theme)
{
    if (!label)
        return;
    lv_obj_set_style_border_color(label, theme.border_color, 0);
    lv_obj_set_style_bg_color(label, theme.card_bg_color, 0);
    lv_obj_t *icon_label = lv_obj_get_child(label, 0);
    if (icon_label)
        lv_obj_set_style_text_color(icon_label, theme.text_color, 0);
    lv_obj_t *text_label = (lv_obj_t *)lv_obj_get_user_data(label);
    if (text_label && !strstr(lv_label_get_text(text_label), "\xC2\xB0"))
        lv_obj_set_style_text_color(text_label, theme.text_color, 0);
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
            lv_obj_set_style_text_color(d.name_label, theme.text_color, 0);
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

    // Host name header.
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, name);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header, theme->text_color, 0);
    lv_obj_set_style_pad_top(header, 2, 0);
    d.name_label = header;

    // Body: two columns of arc + metric cards.
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 320, 218);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_col = lv_obj_create(body);
    lv_obj_t *right_col = lv_obj_create(body);
    for (lv_obj_t *col : {left_col, right_col})
    {
        lv_obj_set_size(col, 158, 218);
        lv_obj_set_style_pad_all(col, 2, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    }

    d.cpu_arc = create_arc(left_col, "CPU", theme->cpu_color);
    d.temp_label = create_compact_label(left_col, LV_SYMBOL_WARNING " Temp: -- C", theme);
    d.gpu_label = create_compact_label(left_col, LV_SYMBOL_CHARGE " GPU: --%", theme);
    d.uptime_label = create_compact_label(left_col, LV_SYMBOL_POWER "  ---", theme);

    d.ram_arc = create_arc(right_col, "RAM", theme->ram_color);
    d.disk_label = create_compact_label(right_col, LV_SYMBOL_DRIVE " Disk: --%", theme);
    d.vram_label = create_compact_label(right_col, LV_SYMBOL_SAVE " VRAM: --%", theme);
    d.net_label = create_compact_label(right_col, LV_SYMBOL_SHUFFLE " Net: ---", theme);

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
    lv_obj_align(dots_cont, LV_ALIGN_BOTTOM_MID, 0, -6);
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

// Format a byte/sec rate compactly (e.g. "1.2M", "640K", "20B").
static void format_rate(float bytes_per_sec, char *buffer, size_t len)
{
    if (bytes_per_sec >= 1024.0f * 1024.0f)
        snprintf(buffer, len, "%.1fM", bytes_per_sec / (1024.0f * 1024.0f));
    else if (bytes_per_sec >= 1024.0f)
        snprintf(buffer, len, "%.1fK", bytes_per_sec / 1024.0f);
    else
        snprintf(buffer, len, "%.0fB", bytes_per_sec);
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
        lv_label_set_text(d.name_label, sys.name);
        lv_obj_set_style_text_color(d.name_label,
                                    sys.up ? lv_color_hex(0x33D17A) : lv_color_hex(0xE0504F), 0);
    }

    // CPU arc: big % value, cores on the info line.
    if (d.cpu_arc.arc)
    {
        lv_obj_t **L = (lv_obj_t **)lv_obj_get_user_data(d.cpu_arc.arc);
        if (L)
        {
            lv_label_set_text(L[0], "CPU");
            snprintf(buf, sizeof(buf), "%d%%", (int)sys.cpu);
            lv_label_set_text(L[1], buf);
            snprintf(buf, sizeof(buf), "%d cores", sys.cores);
            lv_label_set_text(L[2], buf);
        }
        set_arc_value_animated(d.cpu_arc.arc, (int)sys.cpu);
    }

    // RAM arc: % value, with total GB (from system_stats) on the info line.
    if (d.ram_arc.arc)
    {
        lv_obj_t **L = (lv_obj_t **)lv_obj_get_user_data(d.ram_arc.arc);
        if (L)
        {
            lv_label_set_text(L[0], "RAM");
            snprintf(buf, sizeof(buf), "%d%%", (int)sys.mem);
            lv_label_set_text(L[1], buf);
            if (sys.hasMemTotal)
                snprintf(buf, sizeof(buf), "/ %.1f GB", sys.memTotalGB);
            else
                buf[0] = '\0';
            lv_label_set_text(L[2], buf);
        }
        set_arc_value_animated(d.ram_arc.arc, (int)sys.mem);
    }

    if (sys.hasTemp)
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " Temp: %d C", (int)sys.temp);
    else
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " Temp -- ");
    update_compact_label(d.temp_label, buf);

    if (sys.hasGpu)
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " GPU: %d%%", (int)sys.gpu);
    else
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " GPU: --");
    update_compact_label(d.gpu_label, buf);

    char tmp[24];
    format_uptime(sys.uptime, tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), LV_SYMBOL_POWER "  %s", tmp);
    update_compact_label(d.uptime_label, buf);

    snprintf(buf, sizeof(buf), LV_SYMBOL_DRIVE " Disk: %d%%", (int)sys.disk);
    update_compact_label(d.disk_label, buf);

    if (sys.hasVram)
        snprintf(buf, sizeof(buf), LV_SYMBOL_SAVE " VRAM: %d%%", (int)sys.vram);
    else
        snprintf(buf, sizeof(buf), LV_SYMBOL_SAVE " VRAM: --");
    update_compact_label(d.vram_label, buf);

    format_rate(sys.bw, tmp, sizeof(tmp));
    snprintf(buf, sizeof(buf), LV_SYMBOL_SHUFFLE " Net: %s", tmp);
    update_compact_label(d.net_label, buf);
}
