#include <Arduino.h>
#include <LittleFS.h>

#include "bunny.xbm"

int movie_nframes = 0;
int movie_frame = 0;
static Dir dir;
static File file;

uint32_t rgbt_unpack(uint8_t rg, uint8_t bt)
{
    uint8_t nr = rg >> 4;
    uint8_t ng = rg & 0xf;
    uint8_t nb = bt >> 4;
    uint8_t nt = bt & 0xf;

    // swizzle rgbt
    // t is just bits, spread them evenly
    uint32_t t =  (nt & 8) ? 0x80008000 : 0;
             t |= (nt & 4) ? 0x08000800 : 0;
             t |= (nt & 2) ? 0x00800080 : 0;
             t |= (nt & 1) ? 0x00080008 : 0;
    uint32_t b = 0x44444444 << ((8 - nb/1) * 4);
    uint32_t g = 0x22222222 << ((8 - ng/1) * 4);
    uint32_t r = 0x11111111 << ((8 - nr/1) * 4);

    return r | g | b | t;
}

uint32_t rgb_unpack(uint8_t rg, uint8_t bt)
{
    uint8_t nr = rg >> 4;
    uint8_t ng = rg & 0xf;
    uint8_t nb = bt >> 4;
    uint8_t nt = bt & 0xf;

    // swizzle rgbt
    uint32_t b = 0x44444444 << ((8 - nb/1) * 4);
    uint32_t g = 0x22222222 << ((8 - ng/1) * 4);
    uint32_t r = 0x11111111 << ((8 - nr/1) * 4);

    return r | g | b;
}

int movie_open()
{
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return -1;
    }

    dir = LittleFS.openDir("/");

    return 0;
}

int movie_next_file()
{
    if (file) {
        file.close();
    }

    if (!dir.next()) {
        if (!dir.rewind()) {
            Serial.println("No files");
            return -1;
        }
    }

    file = dir.openFile("r");
    if (!file) {
        Serial.println("Failed to open movie");
        return -1;
    }

    movie_nframes = file.size() / (32 * 32);
    movie_frame = 0;
    Serial.printf("Opened %s: %d bytes (%d frames)\n", 
            file.name(), file.size(), movie_nframes);

    return 0;
}

// 32*32*4
static uint8_t frame_buf[32 * 32 * 2];
void movie_get_next_frame_rgbt(uint32_t *dst)
{
    if (!file || !file.available()) {
        movie_next_file();
    }
    if (!file) {
        return;
    }

    file.read(frame_buf, 32 * 32 * 2); // packed values are 16-bit
    ++movie_frame;

    for (uint n = 0; n < 32 * 32; ++n) {
        *dst++ = rgbt_unpack(frame_buf[2 * n], frame_buf[2 * n + 1]);
    }
}

// frames are rrrr gggg bbbb hnib  pcm high nibble
//            rrrr gggg bbbb lnib  pcm low nibble
void movie_get_next_frame_rgbt_pcm(uint32_t *dst, uint8_t *dstpcm)
{
    if (!file || !file.available()) {
        movie_next_file();
    }
    if (!file) {
        return;
    }
    file.read(frame_buf, 32 * 32 * 2); // packed values are 16-bit
    ++movie_frame;

    int pcmcnt = 0;
    uint8_t *oldpcm = dstpcm;

    uint8_t pcm8 = 0;
    for (uint n = 0; n < 32 * 32; ++n) {
        uint8_t rg = frame_buf[2 * n];
        uint8_t bt = frame_buf[2 * n + 1];
        *dst++ = rgb_unpack(rg, bt);

        if ((n & 1) == 0) {
            pcm8 = (bt & 0xf) << 4;
        }
        else {
            pcm8 |= bt & 0xf;
            *dstpcm++ = pcm8;
            pcmcnt++;
        }
    }
    //Serial.printf("pcmcnt=%d dstpcm=%x old=%x\n", pcmcnt, dstpcm, oldpcm);
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
