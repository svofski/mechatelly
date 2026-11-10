#pragma once

enum class MenuMode : int {
    NONE,
    MOVIE,
    RPS,
    HEART
};

extern MenuMode menu_mode;
extern int flash_nframes;

void paint_flash();
void paint_rps();
void menu_do_buttons();
