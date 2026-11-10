#include <Arduino.h>
#include "mecha.h"
#include "movie.h"
#include "text.h"
#include "btn.h"
#include "menu.h"

MenuMode menu_mode = MenuMode::MOVIE;
MenuMode menu_ok_pressed_in = MenuMode::NONE;

int flash_nframes = 0;

static const char * flash_text = nullptr;
static const char * flash_text0 = nullptr;
static int flash_x = 0;
static int flash_x_advance = 0;
static int flash_x_max = 256;
static int flash_x_reset = -38;
static uint8_t flash_font[2] {};
static int text_x = 0;

static char text_buf[32];

void flash_menu_mode()
{
    flash_x = -38;
    flash_x_advance = 2;
    flash_x_max = 256;
    flash_x_reset = -38;
    flash_nframes = goal_fps * 4;
    flash_text0 = nullptr;
    switch (menu_mode) {
        case MenuMode::MOVIE:
            flash_text = "Movie";
            break;
        case MenuMode::RPS:
            flash_text = "RPS";
            break;
        case MenuMode::HEART:
            flash_text = "Test";
            break;
    }
}

void flash_play_pause()
{
    flash_text = " \u23f5";
    flash_font[1] = 1;
    if (freeze_frame) {
        flash_text = " \u23f8";
    }
    flash_x = 0;
    flash_x_advance = 0;
    flash_nframes = GOAL_FPS;
    flash_font[1] = 1;
}

void flash_goal_rps()
{
    static char rps_buf[8];
    snprintf(rps_buf, sizeof(rps_buf) - 1, " !%d!", (int)goal_fps); 
    flash_text0 = " set";
    flash_text = rps_buf;
    flash_x = 0;
    flash_x_advance = 0;
    flash_nframes = GOAL_FPS;
    flash_font[1] = 0;
}

void flash_delay()
{
    static char delay_buf[8];
    snprintf(delay_buf, sizeof(delay_buf) - 1, " +%d", (int)send_pixels_delay); 
    flash_text0 = "H-ofs";
    flash_text = delay_buf;
    flash_x = 0;
    flash_x_advance = 0;
    flash_nframes = GOAL_FPS;
    flash_font[1] = 0;
}


void paint_rps()
{
    text_clear();
    text_draw_str(6, 12, "RPS");
    snprintf(text_buf, sizeof(text_buf) - 1, "  %d", (int)rps); 
    text_draw_str(0, 24, text_buf);
    text_copy_to_frame(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)], 0, 0);
}

void paint_like(const char *msg)
{
    text_clear();
    text_draw_str(-text_x, 24, msg);
    text_copy_to_frame(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)], 0, 0);
    text_x += 1;
    if (text_x >= 256) {
        text_x = -38;
    }
}

void paint_flash()
{
    text_clear();
    if (flash_text0) {
        switch (flash_font[0]) {
            case 0:
                text_draw_str(0, 12, flash_text0);
                break;
            case 1:
                text_draw_symbols(0, 12, flash_text0);
                break;
        }
    }
    switch (flash_font[1]) {
        case 0:
            text_draw_str(-flash_x, 24, flash_text);
            break;
        case 1:
            text_draw_symbols(-flash_x, 24, flash_text);
            break;
    }
    text_copy_to_frame(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)], 0, 0);

    --flash_nframes;
    flash_x += flash_x_advance;
    if (flash_x >= flash_x_max) {
        flash_x = -flash_x_reset;
    }
}

void menu_do_buttons()
{

    btn_poll();
    if (btn_pressed(PIN_BTN_MINUS)) {
        Serial.println("Pressed MINUS");
        switch (menu_mode) {
            case MenuMode::MOVIE:
                movie_prev_file();
                if (freeze_frame) {
                    movie_advance_frame();
                }
                break;
            case MenuMode::RPS:
                goal_fps = constrain(goal_fps - 1, MIN_RPS, MAX_RPS);
                flash_goal_rps();
                break;
            case MenuMode::HEART:
                send_pixels_delay = constrain(send_pixels_delay - 10, 0, 800);
                flash_delay();
                break;
            default:
                break;
        }
    }
    if (btn_pressed(PIN_BTN_PLUS)) {
        Serial.println("Pressed PLUS");
        switch (menu_mode) {
            case MenuMode::MOVIE:
                if (freeze_frame) {
                    movie_advance_frame();
                }
                else {
                    movie_next_file();
                }
                break;
            case MenuMode::RPS:
                goal_fps = constrain(goal_fps + 1, MIN_RPS, MAX_RPS);
                flash_goal_rps();
                break;
            case MenuMode::HEART:
                send_pixels_delay = constrain(send_pixels_delay + 10, 0, 800);
                flash_delay();
                break;
            default:
                break;
        }
    }
    if (btn_pressed(PIN_BTN_OK)) {
        menu_ok_pressed_in = menu_mode;
    }
    if (btn_released(PIN_BTN_OK)) {
        Serial.println("Pressed OK");
        switch (menu_mode) {
            case MenuMode::MOVIE:
                if (menu_ok_pressed_in == MenuMode::MOVIE) {
                    freeze_frame = !freeze_frame;
                    flash_play_pause();
                }
                break;
            case MenuMode::RPS:
                if (menu_ok_pressed_in == MenuMode::RPS) {
                    goal_fps = GOAL_FPS;
                    flash_goal_rps();
                }
                break;
        }
    }
    if (btn_longpress(PIN_BTN_OK)) {
        Serial.println("Longpress OK");
        int nextmode = static_cast<int>(menu_mode) + 1;
        if (nextmode > static_cast<int>(MenuMode::HEART)) {
            nextmode = static_cast<int>(MenuMode::MOVIE);
        }
        menu_mode = static_cast<MenuMode>(nextmode);
        flash_menu_mode();
    }
}
