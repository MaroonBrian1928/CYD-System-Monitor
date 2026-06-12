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

#endif