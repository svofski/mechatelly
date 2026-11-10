#include "pico/platform/sections.h"
#include <Arduino.h>
#define PDM 0
#define PWMAUDIO 1
#define SYNC_INTERRUPT 1

#if PWMAUDIO
#include <PWMAudio.h>
#endif

#include "pico/platform/common.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/multicore.h"
#include "shifter.pio.h"

#include "mecha.h"
#include "motori.h"
#include "movie.h"
#include "text.h"
#include "btn.h"
#include "menu.h"

// subpixel clock = fps * 32 * 32 * 8 
constexpr float SUBPIXEL_CLOCK = GOAL_FPS * 32 * 32 * 8;

// sync
volatile int hsync_interrupt_counter = 0;
volatile int last_hsync_interrupt_counter = 0;
volatile uint64_t last_sync_us = 0;
volatile uint64_t diff_us = 1000;

// frame and pio
PIO shifter_pio = pio1;
int shifter_sm;
int shifter_offset;
int shifter_dma_chan = -1;

// RGBT format (rgb + tone)
// movie packed data: 
// uint16_t: rrrrgggg bbbbtttt, where r,g,b in 0..8, t unsure
// PIO unpacked: 
// uint32_t rgbtrgbt 
//          rgbtrgbt 
//          rgbtrgbt 
//          rgbtrgbt 


uint32_t test_pixels_u32[NPIXELS * 2];
uint8_t *test_pixels_u8 = (uint8_t *)test_pixels_u32;

// pcm sound buffer: sample rate = 1/2 pixel rate, pingpong 
uint32_t pcm_sound_u32[NPIXELS / 2 / sizeof(uint32_t) * 2];
uint8_t *pcm_sound_u8 = (uint8_t *)pcm_sound_u32;

volatile int pingpong_rd = 0;  // write = pingpong_rd ^1

constexpr uint FRAME_NBYTES = NPIXELS * 4;
constexpr uint FRAME_NSAMPLES = NPIXELS / 2;

volatile int send_pixels = 0;
volatile int send_pixels_delay = 80;
int frame_pixel_pos = 0;

bool freeze_frame = false;


#if PWMAUDIO
PWMAudio pwm(1);
uint32_t pwm_read_index = FRAME_NSAMPLES;

void __not_in_flash_func(sync_interrupt)(void);

void pwmaudio_cb()
{
    uint32_t base = pingpong_rd ? FRAME_NSAMPLES : 0;
    while (pwm.availableForWrite()) {
        pwm.write(((int16_t)pcm_sound_u8[base + pwm_read_index] - 128) << 8);
        if (++pwm_read_index >= FRAME_NSAMPLES) {
            pwm_read_index = 0;
        }
    }
}
#endif

int64_t start_frame_cb(alarm_id_t id, void *user_data)
{
    ++send_pixels;
    return 0; // do not reschedule
}

void pio_shifter_init()
{
    shifter_sm = pio_claim_unused_sm(shifter_pio, true);
    shifter_offset = pio_add_program(shifter_pio, &shifter_program);
    float clk_div = (float)clock_get_hz(clk_sys) / (SUBPIXEL_CLOCK);
    shifter_program_init(shifter_pio, shifter_sm, shifter_offset, PIN_LED_R, clk_div);

    Serial.printf("shifter program offset=%d clkdiv=\n", shifter_offset); Serial.println(clk_div);
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    btn_begin(PIN_BTN_PLUS, PIN_BTN_OK, PIN_BTN_MINUS);

    //while (!btn_pressed(PIN_BTN_OK)) tight_loop_contents();

    // delay until the host answers to serial connection
    // a nice safety mechanism during debuggin
    while (!Serial) {
        btn_poll();
        if (btn_pressed(PIN_BTN_OK)) {
            break;
        }
        delay(10); 
    }  

    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    digitalWrite(PIN_LED_R, 0);
    digitalWrite(PIN_LED_G, 0);
    digitalWrite(PIN_LED_B, 0);

    Serial.println("Hello from pipico (philhower)");
    Serial.printf("goal fps=%d subpixel clock=%d\n", (int)GOAL_FPS,
            (int)SUBPIXEL_CLOCK);


    pinMode(PIN_SYNC, INPUT_PULLDOWN);
    // this is the default gpio_set_input_hysteresis_enabled(PIN_SYNC, true); 

    motori_begin();

#if SYNC_INTERRUPT
    attachInterrupt(PIN_SYNC, sync_interrupt, RISING);
#endif

    pio_shifter_init();

    //diagnose_pio(shifter_pio, shifter_sm);

    //create_test_pattern();
    create_bunny(test_pixels_u32);
    movie_begin();
    text_begin();
    text_draw_str(0, 28, "ALL HAIL THE HYPNOTOAD");

#if PWMAUDIO
    pwm.onTransmit(pwmaudio_cb);
    pwm.begin(10240);
#endif
}

void __not_in_flash_func(dma_shift_pixels)()
{
    if (shifter_dma_chan < 0) {
        shifter_dma_chan = dma_claim_unused_channel(true);
    }
    //dma_channel_abort(shifter_dma_chan);
    //pio_sm_clear_fifos(shifter_pio, shifter_sm);

    dma_channel_config_t cfg = dma_channel_get_default_config(shifter_dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_irq_quiet(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(shifter_pio, shifter_sm, true));
    dma_channel_configure(shifter_dma_chan, &cfg, 
            &shifter_pio->txf[shifter_sm], /* write addr */
            &test_pixels_u32[NPIXELS * pingpong_rd],
            dma_encode_transfer_count(NPIXELS - 32), // sacrifice pixels for stability
            false /* start transfer */);
    //pio_sm_clear_fifos(shifter_pio, shifter_sm);
    //while (dma_channel_is_busy(shifter_dma_chan)) tight_loop_contents();
    dma_channel_abort(shifter_dma_chan);
    dma_channel_start(shifter_dma_chan);
}

void __not_in_flash_func(sync_interrupt)()
{
    uint64_t nowus = time_us_64();
    uint64_t newdiff = nowus - last_sync_us;
    if (newdiff < 3000) return; // filter out obvious misfires
    diff_us = newdiff;
    last_sync_us = nowus;

    ++hsync_interrupt_counter;

    // add a little centering offset 
    if (send_pixels_delay > 0) {
        add_alarm_in_us(send_pixels_delay, start_frame_cb, nullptr, false);
    }
    else {
        ++send_pixels;
    }
}

float subpixel_clock = 32 * 32 * 8 * GOAL_FPS;
float clk_div = (float)clock_get_hz(clk_sys) / subpixel_clock;

void __not_in_flash_func(loop)() {
#ifndef SYNC_INTERRUPT
    while ((sio_hw->gpio_in & (1ul << PIN_SYNC)) != 0) tight_loop_contents();
    while ((sio_hw->gpio_in & (1ul << PIN_SYNC)) == 0) tight_loop_contents();
    sync_interrupt();
#endif

    if (send_pixels > 0) {
        --send_pixels;
        if (send_pixels > 0) {
            //Serial.println("underrun!");
            subpixel_clock *= 1.01f;
            send_pixels = 0;
        }

        dma_shift_pixels();

        uint8_t * pcm_frame = &pcm_sound_u8[FRAME_NSAMPLES * (pingpong_rd ^ 1)];

        switch (menu_mode) {
            case MenuMode::MOVIE:
                if (!freeze_frame) {
                    movie_advance_frame();
                }
                movie_get_next_frame_rgbt_pcm(
                        &test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)],
                        pcm_frame);
                break;
            case MenuMode::RPS:
                {
                    movie_heartbeat(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)]);
                    movie_stfu(pcm_frame);
                    if (!flash_nframes) {
                        paint_rps();
                    }
                }
                break;
            case MenuMode::HEART:
                {
                    if (flash_nframes > 0) {
                        movie_clear(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)]);
                    }
                    else {
                        create_bunny(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)]);
                    }
                    movie_stfu(pcm_frame);
                }
                break;
        }

        if (flash_nframes > 0) {
            paint_flash();
        }

        noInterrupts();
        uint64_t diffus_copy = diff_us;
        interrupts();

        // prepare clocks for the next frame
        float exact_rps = 1.0e6f / diffus_copy;
        subpixel_clock = 32 * 32 * 8 * exact_rps;
        clk_div = (float)clock_get_hz(clk_sys) / subpixel_clock;
        pio_sm_set_clkdiv(shifter_pio, shifter_sm, clk_div);
        //exact_sr = 32*32/2 * exact_rps;

        menu_do_buttons();

        if (btn_pressed(PIN_BTN_OK) || btn_pressed(PIN_BTN_PLUS) || btn_pressed(PIN_BTN_MINUS)) {
            movie_beep(&pcm_sound_u8[FRAME_NSAMPLES * (pingpong_rd ^ 1)]);
        }

        pingpong_rd ^= 1;
        pwm_read_index = 0;
    }
}

