#include <U8g2lib.h>
#include "movie.h"

//u8g2_t u8g2;

constexpr uint16_t WIDTH = 128;
constexpr uint16_t HEIGHT = 64;

static uint8_t bitmap[WIDTH * HEIGHT / 8];
//U8G2 u8g2;
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(
        U8G2_R0,
        U8X8_PIN_NONE,  // clock
        U8X8_PIN_NONE,  // data
        U8X8_PIN_NONE,  // cs
        U8X8_PIN_NONE,  // dc
        U8X8_PIN_NONE   // reset
        );

void text_begin()
{
    u8g2.setBufferPtr(bitmap);
    u8g2.begin();
    //u8g2.setFont(u8g2_font_logisoso28_tf);
    //u8g2.setFont(u8g2_font_logisoso16_tf);
    //u8g2.setFont(u8g2_font_inb27_mf);
    u8g2.setFont(u8g2_font_crox5tb_tf);
}

void text_clear()
{
    u8g2.clearBuffer();
}

void text_draw_str(int x, int y, const char *s)
{
    u8g2.clearBuffer();
    u8g2.drawStr(x, y, s);
}

static bool get_pixel(int col, int row)
{
    uint16_t page = row / 8;
    uint16_t byte = (page * WIDTH) + col;
    uint8_t bit = row % 8;
    return (bitmap[byte] & (1 << bit)) != 0;
}

void text_copy_to_frame(uint32_t *dst, int x, int y)
{
    for (int row = y; row < y + 32; ++row) {
        for (int col = x; col < x + 32; ++col) {
            bool p = false;
            if (col >= 0 && col < WIDTH && row >= 0 && row < HEIGHT) {
                p = get_pixel(col, row);
            }
            uint8_t rg = p ? 0x88 : 0x00;
            uint8_t bt = p ? 0x80 : 0x00;
            uint32_t rgbt = rgb_unpack(rg, bt);
            if (rgbt > *dst) {
                *dst = rgbt;
            }
            dst++;
        }
    }
}
