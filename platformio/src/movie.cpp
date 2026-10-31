#include <Arduino.h>
#include <LittleFS.h>

#include "bunny.xbm"

int movie_nframes = 0;
int movie_frame = 0;
static File file;

uint32_t rgbt_unpack(uint8_t rg, uint8_t bt)
{
    uint8_t nr = rg >> 4;
    uint8_t ng = rg & 0xf;
    uint8_t nb = bt >> 4;
    uint8_t nt = bt & 0xf;

    // swizzle rgbt
    //uint32_t t = 0x88888888 << ((8 - nt/1) * 4);
    uint32_t t =  (nt & 8) ? 0x80008000 : 0;
             t |= (nt & 4) ? 0x08000800 : 0;
             t |= (nt & 2) ? 0x00800080 : 0;
             t |= (nt & 1) ? 0x00080008 : 0;
    uint32_t b = 0x44444444 << ((8 - nb/1) * 4);
    uint32_t g = 0x22222222 << ((8 - ng/1) * 4);
    uint32_t r = 0x11111111 << ((8 - nr/1) * 4);

    return r | g | b | t;
}

int movie_open()
{
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return -1;
    }

    //file = LittleFS.open("/ccarrot.bin", "r");
    file = LittleFS.open("/cboop.bin", "r");
    if (!file) {
        Serial.println("Failed to open movie");
        return -1;
    }

    Serial.print("Dataset opened successfully. Size: ");
    Serial.print(file.size());
    Serial.println(" bytes.");

    movie_nframes = file.size() / (32 * 32);
    movie_frame = 0;

    return 0;
}

// 32*32*4
static uint8_t frame_buf[32 * 32 * 2];
void movie_get_next_frame_rgbt(uint32_t *dst)
{
    ++movie_frame;
    file.read(frame_buf, 32 * 32 * 2); // packed values are 16-bit
    if (!file.available()) {
        file.seek(0, SeekSet);
        movie_frame = 0;
    }

    for (uint n = 0; n < 32 * 32; ++n) {
        *dst++ = rgbt_unpack(frame_buf[2 * n], frame_buf[2 * n + 1]);
    }
}

void movie_get_next_frame(uint8_t *dst)
{
    ++movie_frame;
    file.read(dst, 32 * 32);
    if (!file.available()) {
        file.seek(0, SeekSet);
        movie_frame = 0;
    }
}

void create_bunny(uint32_t *dst)
{
    int ofs = 0;
    constexpr int stride = 32/8;
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < stride; ++col) {
            uint8_t pixels = bunny_bits[row * stride + col];

            for (int i = 0; i < 8; ++i) {
                uint8_t rg = (pixels & 0x01) ? 0x80 : 0x00;
                uint8_t bt = (pixels & 0x01) ? 0x00 : 0x00;
                *dst = rgbt_unpack(rg, bt);
                //Serial.printf("B[%02x,%02x]->%08x\n", rg, bt, *dst);
                dst++;
                pixels >>= 1;
            }
        }
    }
}

#if 0
void create_test_pattern(uint32_t *dst)
{
    for (int r = 0; r < 32; ++r) {
        for (int i = 0; i < 32; ++i) {
            test_pixels_u8[r * 32 + i] = ((i & 4) ^ (r & 4)) ? 0xff : 0x00;
        }
    }

    for (int i = 0; i < 32/4; ++i) {
        Serial.printf("%d: %08x\n", i, test_pixels_u32[i]);
    }
    Serial.println("------------");
}
#endif
