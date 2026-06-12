#ifndef GUI_H
#define GUI_H

#include <lvgl.h>
#include "config.h"

struct ArcWithLabel
{
    lv_obj_t *arc;
    lv_obj_t *label;
};

ArcWithLabel create_arc(lv_obj_t *parent, const char *text, lv_color_t color);
lv_obj_t *create_button_label(lv_obj_t *parent, const char *text, const ThemeColors *theme);
lv_obj_t *create_compact_label(lv_obj_t *parent, const char *text, const ThemeColors *theme);
void update_compact_label(lv_obj_t *btn, const char *text);
void update_arc_label(lv_obj_t *label, const char *text);

void create_system_monitor_gui();

void set_arc_value_animated(lv_obj_t *arc, int32_t value, uint32_t duration = 500);

extern lv_obj_t *cpu_label;
extern lv_obj_t *ram_label;
extern lv_obj_t *disk_label;
extern lv_obj_t *uptime_label;
extern lv_obj_t *network_label;
extern lv_obj_t *cores_label;
extern lv_obj_t *total_ram_label;
extern lv_obj_t *temp_label;
extern lv_obj_t *gpu_label;
extern lv_obj_t *vram_label;

extern ArcWithLabel cpu_arc_obj;
extern ArcWithLabel ram_arc_obj;

// Page 2 (container list). The multi-line label is filled by glances_api.cpp;
// gui_container_page_active() lets the data layer skip the fetch when hidden.
// container_header is the title label, updated with the live container count.
extern lv_obj_t *container_label;
extern lv_obj_t *container_header;
bool gui_container_page_active();

// Called by the touch driver on each tap to advance to the next page (wraps).
void gui_next_page();

// Auto-cycle between the UI pages (NOT screen orientation). interval_sec is how
// long each page is shown before advancing.
void gui_set_auto_rotate(bool enabled, uint32_t interval_sec);

// Shared monospace (UNSCII_8) column layout so the header and the data rows line
// up. The status is a recolored "*" marker (UNSCII is ASCII-only, so no real
// bullet glyph); green = running/healthy, amber = starting/paused, red = other.
// Widths chosen to fill the 320px width (~38 mono chars) without wrapping.
//          dot(1) name(23)            cpu(5) mem(5)
#define CONTAINER_HDR_FMT "  %-23s %5s %5s"
#define CONTAINER_ROW_FMT "#%06X *# %-23.23s %5.1f %5s\n"

#endif