// gui.h
#pragma once

#include "u8g2.h"
#include "gui.h"

typedef struct {
    u8g2_t* u8g2;
    int delay_ms;
} splash_draw_config_t;

void gui_draw_splashes_task(splash_draw_config_t*);

void gui_draw_splashes_blocking(u8g2_t* u8g2, int delay_ms);

void gui_clear_screen(u8g2_t* u8g2);

void gui_print_string(u8g2_t*, const char*);

void gui_print_lines_select(u8g2_t* u8g2, int x, int y, int select_index, char** lines, int lines_count);

void gui_draw_bars(u8g2_t* u8g2, int x, int y, int col_width, int height, int scale, int* data, int count);

void gui_update_bar(u8g2_t* u8g2, int x, int y, int col_width, int height, int scale, int data, int index);

void gui_draw_main_menu(u8g2_t*);