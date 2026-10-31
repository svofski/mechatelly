#include <Arduino.h>
#include "pico/platform/common.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/multicore.h"
#include "shifter.pio.h"

#include "movie.h"

constexpr float GOAL_FPS = 20; // 10..40 is a good range

// subpixel clock = fps * 32 * 32 * 8 
constexpr float SUBPIXEL_CLOCK = GOAL_FPS * 32 * 32 * 8;

constexpr int KICKSTART_TSTEP = 50*1000;
constexpr int KICKSTART_DUTY = 100;
constexpr int MIN_DUTY = 30;
constexpr int MAX_DUTY = 240;

constexpr int PIN_U = 18;
constexpr int PIN_V = 19;
constexpr int PIN_W = 20;

constexpr int PIN_SYNC = 16;  // optical sensor for line or frame sync
constexpr int PIN_HALL = 17;  // hall sensor for BLDC commutation

constexpr int PIN_LED_R = 12;
constexpr int PIN_LED_G = 13;
constexpr int PIN_LED_B = 14;
constexpr int PIN_LED_T = 15;

volatile int32_t interrupt_counter = 0, last_interrupt_counter = 0;

unsigned long start_millis = 0;
unsigned long last_millis = 0;
unsigned long last_int_time = 0;
uint32_t tstep = 1000;
volatile int seq_pos = 0;

int ramp_us = KICKSTART_TSTEP;
int ramp_us_step = 0; // don't ramp, just prod
int ramp_us_min = 2000;

bool ramp_done = false;

float fduty = 0;
int duty = 0;

volatile float rps = GOAL_FPS;
volatile float lps = GOAL_FPS;
volatile float exact_rps = GOAL_FPS;
volatile uint64_t diff_us = 1000;

volatile bool step_reset = false;
volatile bool request_restart_motor = false;
void motor_loop();

// sync
volatile int hsync_interrupt_counter = 0;
volatile int last_hsync_interrupt_counter = 0;
volatile uint64_t last_sync_us = 0;

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

constexpr int NPIXELS = 32 * 32;

uint32_t test_pixels_u32[NPIXELS * 2];
uint8_t *test_pixels_u8 = (uint8_t *)test_pixels_u32;

int pingpong_rd = 0;  // write = pingpong_rd ^1

constexpr int FRAME_NBYTES = NPIXELS * 4;

volatile int send_pixels = 0;
int frame_pixel_pos = 0;

// measure idle time between frames
//uint64_t idle_us_prev = micros();
//uint64_t idle_us_avg = 0;

void restart_motor()
{
    if (get_core_num() == 0) {
        request_restart_motor = true;
        Serial.println("restart_motor requested");
    }
    analogWriteFreq(20000);
    analogWriteResolution(8);

    analogWrite(PIN_U, 0);
    analogWrite(PIN_V, 0);
    analogWrite(PIN_W, 0);

    delay(250);

    noInterrupts();

    duty = KICKSTART_DUTY;
    fduty = duty;
    ramp_us = KICKSTART_TSTEP;
    ramp_done = false;
    start_millis = millis();
    last_int_time = millis();
    interrupt_counter = 0;
    last_interrupt_counter = 0;
    hsync_interrupt_counter = 0;
    last_sync_us = time_us_64();

    interrupts();
}

void hall_interrupt()
{
    interrupt_counter++;

    unsigned long now = micros();
    unsigned long elapsed = now - last_int_time;
    last_int_time = now;
    if (elapsed > 1000) {
        tstep = elapsed / 6;
    }

    if (ramp_done) {
        noInterrupts();
        seq_pos = 0 * 3;
        step_reset = true;
        interrupts();
    }
}

int64_t start_frame_cb(alarm_id_t id, void *user_data)
{
    ++send_pixels;
    return 0; // do not reschedule
}

void sync_interrupt()
{
    uint64_t nowus = time_us_64();
    uint64_t newdiff = nowus - last_sync_us;
    if (newdiff < 3000) return; // filter out obvious misfires
    diff_us = newdiff;
    last_sync_us = nowus;

    ++hsync_interrupt_counter;

    // add a little centering offset 
    //add_alarm_in_us(diff_us/32/8, start_frame_cb, nullptr, false);
    ++send_pixels;
}

void pio_shifter_init()
{
    shifter_sm = pio_claim_unused_sm(shifter_pio, true);
    shifter_offset = pio_add_program(shifter_pio, &shifter_program);
    float clk_div = (float)clock_get_hz(clk_sys) / (SUBPIXEL_CLOCK);
    shifter_program_init(shifter_pio, shifter_sm, shifter_offset, PIN_LED_R, clk_div);

    Serial.printf("shifter program offset=%d clkdiv=\n", shifter_offset); Serial.println(clk_div);

    //create_test_pattern();
    create_bunny(test_pixels_u32);
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    // delay until the host answers to serial connection
    // a nice safety mechanism during debuggin
    while (!Serial) {
        delay(10); 
    }  

    pinMode(PIN_U, OUTPUT);
    pinMode(PIN_V, OUTPUT);
    pinMode(PIN_W, OUTPUT);

    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    digitalWrite(PIN_LED_R, 0);
    digitalWrite(PIN_LED_G, 0);
    digitalWrite(PIN_LED_B, 0);

    digitalWrite(PIN_U, 0);
    digitalWrite(PIN_V, 0);
    digitalWrite(PIN_W, 0);

    Serial.println("Hello from pipico (philhower)");
    Serial.printf("goal fps=%d subpixel clock=%d\n", (int)GOAL_FPS,
            (int)SUBPIXEL_CLOCK);

    analogWriteFreq(20000);
    analogWriteResolution(8);

    pinMode(PIN_HALL, INPUT);

    pinMode(PIN_SYNC, INPUT_PULLDOWN);
    // this is the default gpio_set_input_hysteresis_enabled(PIN_SYNC, true); 

    last_millis = millis();

    multicore_launch_core1(motor_loop);

    attachInterrupt(PIN_SYNC, sync_interrupt, RISING);

    pio_shifter_init();

    //diagnose_pio(shifter_pio, shifter_sm);

    movie_open();
}

// trapezoidal sequence
// U V W
const uint8_t comm_seq[] { 
    1, 0, 0,
    1, 1, 0,
    0, 1, 0,
    0, 1, 1,
    0, 0, 1,
    1, 0, 1};

void motor_loop()
{
    attachInterrupt(PIN_HALL, hall_interrupt, FALLING);
    restart_motor();

    for (;;) {
        noInterrupts();
        int sp = seq_pos;
        seq_pos = seq_pos - 3;
        if (seq_pos < 0) seq_pos = sizeof(comm_seq) - 3;
        interrupts();

        analogWrite(PIN_U, comm_seq[sp + 0] ? duty : 0);
        analogWrite(PIN_V, comm_seq[sp + 1] ? duty : 0);
        analogWrite(PIN_W, comm_seq[sp + 2] ? duty : 0);

        if (ramp_done) {
            uint32_t dly = tstep;
            while (dly > 0 && !step_reset) {
                uint32_t thistime = dly > 25 ? 25 : dly;
                delayMicroseconds(thistime);
                dly -= thistime;
            }
            step_reset = false;
        }
        else {
            delayMicroseconds(ramp_us);

            if (ramp_us - ramp_us_step == ramp_us_min || interrupt_counter > 6) {
                Serial.print("ramp up complete; tstep="); Serial.println(tstep);

                ramp_done = true;
                last_millis = millis();
            }
            else {
                if (ramp_us > ramp_us_min) ramp_us -= ramp_us_step;

                if (millis() - start_millis > 1000) {
                    Serial.println("stall 1");
                    restart_motor();
                }
            }
        }

        if (request_restart_motor) {
            request_restart_motor = false;
            restart_motor();
        }

        unsigned long new_millis = millis();
        constexpr float fac = 0.5f; // control loop per second
        if (new_millis - last_millis >= 1000/fac) {
            last_millis = new_millis;

            rps = (interrupt_counter - last_interrupt_counter) / 6.0f * fac;
            //lps = (hsync_interrupt_counter - last_hsync_interrupt_counter) / 32.f * fac;
            lps = (hsync_interrupt_counter - last_hsync_interrupt_counter) / 1.0f * fac;
            last_hsync_interrupt_counter = hsync_interrupt_counter;

            if (ramp_done && (rps < GOAL_FPS / 4)) {
                Serial.printf("stall 2: rps=%f interrupt_counter=%d last=%d\n", rps, interrupt_counter, last_interrupt_counter);
                restart_motor();
                continue;
            }

            float error = rps - GOAL_FPS;
            constexpr float PID_I = 1.0f / fac;
            if (fabs(error) > 0.1f) {
                if (error > 0) {
                    fduty -= error * PID_I;
                }
                else if (error < 0) {
                    fduty += fabs(error * PID_I);
                }
                duty = constrain(round(fduty), MIN_DUTY, MAX_DUTY);
            }

            //Serial.print("interrupt_counter"); Serial.print(interrupt_counter);
            Serial.printf("duty=%d ", duty);
            Serial.print(" tstep="); Serial.print(tstep);
            Serial.print(" rps="); Serial.print(rps);
            Serial.print(" lps="); Serial.print(lps);
            Serial.print(" diff_us="); Serial.print(diff_us);
            Serial.print(" hsync="); Serial.println(hsync_interrupt_counter);
            //Serial.print(" idle_us="); Serial.println(idle_us_avg);
            last_interrupt_counter = interrupt_counter; 
        }
    }
}

void dma_shift_pixels()
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

void loop() {
    while (send_pixels > 0) {
        //idle_us_avg = (micros() - idle_us_prev) / 2;
        //idle_us_prev = micros();
        noInterrupts();
        exact_rps = 1.0e6f / diff_us;
        float subpixel_clock = 32*32*8 * exact_rps;
        interrupts();

        --send_pixels;
        // detect overruns
        if (send_pixels > 0) {
            //Serial.println("underrun!");
            subpixel_clock *= 1.01f;
            send_pixels = 0;
        }

        float clk_div = (float)clock_get_hz(clk_sys) / subpixel_clock;
        shifter_program_init(shifter_pio, shifter_sm, shifter_offset, PIN_LED_R, clk_div);

        dma_shift_pixels();

        // freeze frame
        //if (movie_frame >= 1)
        //    continue;

        movie_get_next_frame_rgbt(&test_pixels_u32[NPIXELS * (pingpong_rd ^ 1)]);
        pingpong_rd ^= 1;
    }
}

