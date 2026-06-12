#include "gui.h"
#include "settings_manager.h"
#include <Arduino.h>
#include <stdio.h>

lv_obj_t *cpu_label = NULL;
lv_obj_t *ram_label = NULL;
lv_obj_t *disk_label = NULL;
lv_obj_t *uptime_label = NULL;
lv_obj_t *network_label = NULL;
lv_obj_t *cores_label = NULL;
lv_obj_t *total_ram_label = NULL;
lv_obj_t *temp_label = NULL;
lv_obj_t *gpu_label = NULL;
lv_obj_t *vram_label = NULL;
ArcWithLabel cpu_arc_obj = {NULL, NULL};
ArcWithLabel ram_arc_obj = {NULL, NULL};

// ---- Touch UI state: swipe paging -----------------------------------------
#define PAGE_COUNT 2

static lv_obj_t *pages[PAGE_COUNT] = {NULL, NULL};
static lv_obj_t *page_dots[PAGE_COUNT] = {NULL, NULL};
static int current_page = 0;

// Page 2 (Docker containers). Populated from Glances by glances_api.cpp; the
// label holds one line per container (name / CPU% / memory). container_header is
// the title label, updated with the live container count.
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

    if (strstr(text, LV_SYMBOL_DOWNLOAD) && strstr(text, LV_SYMBOL_UPLOAD))
    {
        lv_obj_t *text_label = lv_label_create(btn);
        lv_label_set_text(text_label, text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(text_label, theme->text_color, 0);
        lv_obj_set_width(text_label, 140);
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(text_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_user_data(btn, text_label);
        return btn;
    }

    if (!strstr(text, LV_SYMBOL_DOWNLOAD) || !strstr(text, LV_SYMBOL_UPLOAD))
    {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    bool is_temp = strstr(text, "Temp:") != NULL;

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
    {
        const char *text_start = space_pos + 1;
        lv_label_set_text(text_label, text_start);
    }
    else
    {
        lv_label_set_text(text_label, "");
    }
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);
    if (!strstr(text, "Temp:"))
    {
        lv_obj_set_style_text_color(text_label, theme->text_color, 0);
    }
    lv_obj_set_user_data(btn, text_label);

    return btn;
}

void update_compact_label(lv_obj_t *btn, const char *text)
{
    lv_obj_t *text_label = (lv_obj_t *)lv_obj_get_user_data(btn);

    if (strstr(text, LV_SYMBOL_DOWNLOAD) && strstr(text, LV_SYMBOL_UPLOAD))
    {
        lv_label_set_text(text_label, text);
        return;
    }

    if (strstr(text, "Temp:"))
    {
        const char *temp_start = strstr(text, "Temp:") + 5;
        int temperature = 0;

        if (sscanf(temp_start, "%d", &temperature) == 1)
        {
            lv_color_t temp_color;
            if (temperature < 40)
            {
                temp_color = lv_color_hex(0x00FF44);
            }
            else if (temperature < 50)
            {
                temp_color = lv_color_hex(0xFFAA00);
            }
            else
            {
                temp_color = lv_color_hex(0xFF4444);
            }

            lv_obj_set_style_text_color(text_label, temp_color, 0);
        }
        else
        {
            Serial.println("Failed to parse temperature!");
        }

        lv_label_set_text(text_label, temp_start);
        return;
    }

    const char *space_pos = strchr(text, ' ');
    if (space_pos)
    {
        const char *text_start = space_pos + 1;
        lv_label_set_text(text_label, text_start);
    }
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

void applyTheme(bool darkMode)
{
    const ThemeColors &theme = SettingsManager::getCurrentTheme();
    lv_obj_set_style_bg_color(lv_scr_act(), theme.bg_color, 0);

    if (cpu_arc_obj.arc)
    {
        lv_obj_set_style_arc_color(cpu_arc_obj.arc, theme.cpu_color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(cpu_arc_obj.arc, lv_color_darken(theme.cpu_color, LV_OPA_30), LV_PART_MAIN);
        lv_obj_t **labels = (lv_obj_t **)lv_obj_get_user_data(cpu_arc_obj.arc);
        if (labels)
        {
            lv_obj_set_style_text_color(labels[0], theme.text_color, 0);
            lv_obj_set_style_text_color(labels[1], theme.text_color, 0);
            lv_obj_set_style_text_color(labels[2], lv_color_hex(0x808080), 0);
        }
    }

    if (ram_arc_obj.arc)
    {
        lv_obj_set_style_arc_color(ram_arc_obj.arc, theme.ram_color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ram_arc_obj.arc, lv_color_darken(theme.ram_color, LV_OPA_30), LV_PART_MAIN);

        lv_obj_t **labels = (lv_obj_t **)lv_obj_get_user_data(ram_arc_obj.arc);
        if (labels)
        {

            lv_obj_set_style_text_color(labels[0], theme.text_color, 0);
            lv_obj_set_style_text_color(labels[1], theme.text_color, 0);

            lv_obj_set_style_text_color(labels[2], lv_color_hex(0x808080), 0);
        }
    }

    lv_obj_t *compact_labels[] = {
        temp_label,
        gpu_label,
        uptime_label,
        disk_label,
        vram_label,
        network_label};

    for (lv_obj_t *label : compact_labels)
    {
        if (label)
        {
            lv_obj_set_style_border_color(label, theme.border_color, 0);
            lv_obj_set_style_bg_color(label, theme.card_bg_color, 0);
            lv_obj_t *icon_label = lv_obj_get_child(label, 0);
            if (icon_label)
            {
                lv_obj_set_style_text_color(icon_label, theme.text_color, 0);
            }
            lv_obj_t *text_label = (lv_obj_t *)lv_obj_get_user_data(label);
            if (text_label)
            {
                if (!strstr(lv_label_get_text(text_label), "°C"))
                {
                    lv_obj_set_style_text_color(text_label, theme.text_color, 0);
                }
            }
        }
    }

    // Keep the container page legible after a theme change.
    if (container_label)
        lv_obj_set_style_text_color(container_label, theme.text_color, 0);
}

// ---- Swipe paging ---------------------------------------------------------

static void update_dots()
{
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        if (!page_dots[i])
            continue;
        bool active = (i == current_page);
        // Active dot: full opacity and larger; inactive: dim and small.
        lv_obj_set_style_bg_opa(page_dots[i], active ? LV_OPA_COVER : LV_OPA_20, 0);
        int sz = active ? 9 : 5;
        lv_obj_set_size(page_dots[i], sz, sz);
        lv_obj_set_style_radius(page_dots[i], sz / 2, 0);
    }
}

bool gui_container_page_active()
{
    return current_page == 1;
}

// Timer that auto-cycles through the UI pages when enabled (see
// gui_set_auto_rotate). This switches which page is shown -- it does NOT change
// the screen's orientation.
static lv_timer_t *rotate_timer = NULL;

// Called by the touch driver on each screen tap: advance to the next page,
// wrapping back to the first. With two pages this just toggles between them.
void gui_next_page()
{
    current_page = (current_page + 1) % PAGE_COUNT;
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        if (i == current_page)
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    update_dots();

    // A manual page change restarts the auto-cycle countdown so it doesn't
    // immediately flip again right after you tap.
    if (rotate_timer)
        lv_timer_reset(rotate_timer);
}

static void rotate_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    gui_next_page();
}

// Enable/disable automatic cycling between the UI pages and set how long each
// page is shown (in seconds). Persisted by SettingsManager; applied here.
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

    // Monospace column header, generated from the shared layout so it lines up
    // with the data rows produced in glances_api.cpp::updateContainerData().
    char hdr[40];
    snprintf(hdr, sizeof(hdr), CONTAINER_HDR_FMT, "Name", "CPU%", "MEM");
    lv_obj_t *colhead = lv_label_create(parent);
    lv_label_set_text(colhead, hdr);
    lv_obj_set_style_text_font(colhead, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(colhead, lv_color_hex(0x808080), 0);

    // Scrollable viewport holding one multi-line label (a row per container).
    lv_obj_t *view = lv_obj_create(parent);
    lv_obj_remove_style_all(view);
    lv_obj_set_width(view, lv_pct(100));
    lv_obj_set_flex_grow(view, 1);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_set_scroll_dir(view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view, LV_SCROLLBAR_MODE_AUTO);

    container_label = lv_label_create(view);
    lv_label_set_long_mode(container_label, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(container_label, true); // colours the status column
    lv_obj_set_width(container_label, lv_pct(100));
    lv_obj_set_style_text_font(container_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(container_label, theme->text_color, 0);
    lv_label_set_text(container_label, "Loading containers...");
}

void create_system_monitor_gui()
{
    const ThemeColors *theme = DARK_MODE ? &dark_theme : &light_theme;
    lv_obj_set_style_bg_color(lv_scr_act(), theme->bg_color, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    // Transparent full-screen page containers; only the active one is shown.
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        lv_obj_t *page = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, 320, 240);
        lv_obj_center(page);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        pages[i] = page;
        if (i != 0)
            lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Page 0: the existing dashboard ----
    lv_obj_t *main_cont = lv_obj_create(pages[0]);
    if (!main_cont)
        return;

    lv_obj_set_size(main_cont, 320, 240);
    lv_obj_set_style_pad_all(main_cont, 2, 0);
    lv_obj_set_style_bg_opa(main_cont, 0, 0);
    lv_obj_set_style_border_width(main_cont, 0, 0);
    lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_col = lv_obj_create(main_cont);
    if (!left_col)
        return;

    lv_obj_t *right_col = lv_obj_create(main_cont);
    if (!right_col)
        return;

    lv_obj_set_size(left_col, 158, 240);
    lv_obj_set_style_pad_all(left_col, 2, 0);
    lv_obj_set_style_bg_opa(left_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(left_col, 0, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(right_col, 158, 240);
    lv_obj_set_style_pad_all(right_col, 2, 0);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);

    cpu_arc_obj = create_arc(left_col, "CPU", theme->cpu_color);
    if (!cpu_arc_obj.arc || !cpu_arc_obj.label)
        return;

    ram_arc_obj = create_arc(right_col, "RAM", theme->ram_color);
    if (!ram_arc_obj.arc || !ram_arc_obj.label)
        return;

    temp_label = create_compact_label(left_col, LV_SYMBOL_WARNING " Temp: -- °C", theme);
    if (!temp_label)
        return;

    gpu_label = create_compact_label(left_col, LV_SYMBOL_CHARGE " GPU: --%", theme);
    if (!gpu_label)
        return;

    uptime_label = create_compact_label(left_col, LV_SYMBOL_POWER "  ---", theme);
    if (!uptime_label)
        return;

    disk_label = create_compact_label(right_col, LV_SYMBOL_DRIVE " Array: ---%", theme);
    if (!disk_label)
        return;

    vram_label = create_compact_label(right_col, LV_SYMBOL_SAVE " VRAM: ---%", theme);
    if (!vram_label)
        return;

    network_label = create_compact_label(right_col, LV_SYMBOL_DOWNLOAD " --- " LV_SYMBOL_UPLOAD " ---", theme);
    if (!network_label)
        return;

    // ---- Page 1: Docker container list (populated by glances_api.cpp) ----
    build_container_page(pages[1], theme);

    // Page indicator dots (kept above the pages so they persist across swipes).
    lv_obj_t *dots = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, 64, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 8, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_bg_color(d, theme->cpu_color, 0);
        page_dots[i] = d;
    }
    update_dots(); // sets per-dot size/opacity for the active page

    // Swipe handling is done in the touch driver, which calls gui_on_swipe().

    SettingsManager::setThemeChangeCallback(applyTheme);

    applyTheme(SettingsManager::getDarkMode());
}
