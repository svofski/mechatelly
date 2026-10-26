#include <Arduino.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "shifter.pio.h"

#include "bunny.xbm"

#define GRUU_PICO 1

// timer control loop runs too hot, perhaps needs substep adjustment
// runs +10..15 duty cycle hotter for same rpm: 111 vs 125 for 25 rps
#define WITH_TIMER 0
#define CORE1_MOTOR 1

constexpr float GOAL_FPS = 20; // 10..40 is a good range

// subpixel clock = fps * 32 * 32 * 8 
constexpr float SUBPIXEL_CLOCK = GOAL_FPS * 32 * 32 * 8;

constexpr int KICKSTART_TSTEP = 50*1000;
constexpr int KICKSTART_DUTY = 80;
constexpr int MIN_DUTY = 30;
constexpr int MAX_DUTY = 240;

constexpr int PIN_U = 18;
constexpr int PIN_V = 19;
constexpr int PIN_W = 20;

constexpr int PIN_SYNC = 16;  // optical sensor for line or frame sync
constexpr int PIN_HALL = 17;  // hall sensor for BLDC commutation

constexpr int PIN_LED_R = 13;
constexpr int PIN_LED_G = 14;
constexpr int PIN_LED_B = 15;

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

#if WITH_TIMER
repeating_timer_t motor_timer;
bool timer_callback(repeating_timer_t *rt);
void start_timer(int32_t us);
volatile int timer_cnt = 0;
#endif
#if CORE1_MOTOR
volatile bool step_reset = false;
volatile bool request_restart_motor = false;
void motor_loop();
#endif

// sync
volatile int hsync_interrupt_counter = 0;
volatile int last_hsync_interrupt_counter = 0;
volatile uint64_t last_sync_us = 0;

// frame and pio
PIO shifter_pio = pio1;
int shifter_sm;
int shifter_offset;

constexpr int NPIXELS = 32 * 32;
uint32_t test_pixels_u32[NPIXELS/4];
uint8_t *test_pixels_u8 = (uint8_t *)test_pixels_u32;

volatile int send_pixels = 0;
int frame_pixel_pos = 0;
constexpr uint32_t frame_end_pos = NPIXELS/4;


void restart_motor()
{
#if CORE1_MOTOR
    if (get_core_num() == 0) {
        request_restart_motor = true;
        Serial.println("restart_motor requested");
    }
#endif
#if WITH_TIMER
    cancel_repeating_timer(&motor_timer);
#endif
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

#if WITH_TIMER
    start_timer(ramp_us);
#endif
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
#if WITH_TIMER
        seq_pos = 5 * 3;
        start_timer(tstep);
#endif
#if CORE1_MOTOR
        seq_pos = 0 * 3;
        step_reset = true;
#endif
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
    add_alarm_in_us(diff_us/32/8, start_frame_cb, nullptr, false);
    //++send_pixels; // no delay
}

void diagnose_pio(PIO pio, uint sm) {
    uint pio_index = pio_get_index(pio);
    Serial.printf("\n=== PIO %d | State Machine %d Diagnostic ===\n", pio_index, sm);

    // 1. Check Global Enable Register (CTRL)
    uint32_t ctrl = pio->ctrl;
    bool is_enabled = (ctrl & (1u << sm)) != 0;
    Serial.printf("State Machine Enabled: %s (CTRL Reg: 0x%08X)\n", is_enabled ? "YES" : "NO", ctrl);

    // 2. Check FIFO Status (FSTAT Register)
    uint32_t fstat = pio->fstat;
    bool tx_full  = (fstat & (1u << (PIO_FSTAT_TXFULL_LSB + sm))) != 0;
    bool tx_empty = (fstat & (1u << (PIO_FSTAT_TXEMPTY_LSB + sm))) != 0;
    Serial.printf("TX FIFO State:        %s %s (FSTAT Reg: 0x%08X)\n", 
                  tx_full ? "[FULL - BLOCKED]" : "", tx_empty ? "[EMPTY]" : "[HAS DATA]");

    // 3. Inspect Current Program Counter (SM PC)
    uint32_t pc = pio_sm_get_pc(pio, sm);
    Serial.printf("Current Execution PC:  %d (Memory Address Slot)\n", pc);

    // 4. Force Capture of Registers X, Y, and OSR
    // We safely use pio_sm_exec to make the SM output its registers to us on demand
    uint32_t current_instr = pio->sm[sm].instr;
    Serial.printf("Next Hardware Instr:  0x%08X\n", current_instr);
    Serial.println("============================================\n");
}

void create_bunny()
{
    int ofs = 0;
    constexpr int stride = 32/8;
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < stride; ++col) {
            uint8_t pixels = bunny_bits[row * stride + col];

            for (int i = 0; i < 8; ++i) {
                test_pixels_u8[ofs++] = (pixels & 0x01) ? 0xff : 0x00;
                pixels >>= 1;
            }
        }
    }
}

void create_test_pattern()
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

void shifter_init()
{
    shifter_sm = pio_claim_unused_sm(shifter_pio, true);
    shifter_offset = pio_add_program(shifter_pio, &shifter_program);
    float clk_div = (float)clock_get_hz(clk_sys) / (SUBPIXEL_CLOCK);
    shifter_program_init(shifter_pio, shifter_sm, shifter_offset, PIN_LED_R, clk_div);

    Serial.printf("shifter program offset=%d clkdiv=\n", shifter_offset); Serial.println(clk_div);

    //create_test_pattern();
    create_bunny();
}

void setup()
{
    Serial.begin(115200);

    delay(1000);
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

#if CORE1_MOTOR
    multicore_launch_core1(motor_loop);
#else
    attachInterrupt(PIN_HALL, hall_interrupt, FALLING);
    restart_motor();
#endif

    attachInterrupt(PIN_SYNC, sync_interrupt, RISING);

    shifter_init();

    //diagnose_pio(shifter_pio, shifter_sm);
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

#if WITH_TIMER
bool timer_callback(repeating_timer_t *rt)
{
    ++timer_cnt;

    int sp = seq_pos;
    analogWrite(PIN_U, comm_seq[sp + 0] ? duty : 0);
    analogWrite(PIN_V, comm_seq[sp + 1] ? duty : 0);
    analogWrite(PIN_W, comm_seq[sp + 2] ? duty : 0);

    noInterrupts();
    seq_pos = seq_pos - 3;
    if (seq_pos < 0) seq_pos = sizeof(comm_seq) - 3;
    interrupts();

    if (!ramp_done) {
        if (interrupt_counter > 6) {
            ramp_done = true;
            last_millis = millis();
        }
    }

    return true;
}

void start_timer(int32_t us)
{
    cancel_repeating_timer(&motor_timer);
    if (!add_repeating_timer_us(-us, timer_callback, nullptr, &motor_timer)) {
        Serial.println("Failed to create timer");
    }
}
#endif

#if CORE1_MOTOR
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
                //return;
            }

            float error = rps - GOAL_FPS;
            constexpr float PID_I = 1.f / fac;
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
#if WITH_TIMER
            Serial.printf(" timer_cnt=%d\n", timer_cnt);
#endif
            last_interrupt_counter = interrupt_counter; 
        }
    }
}
#endif

void loop() {
    while (send_pixels > 0) {
        noInterrupts();
        exact_rps = 1.0e6f / diff_us;
        float subpixel_clock = 32*32*8 * exact_rps;
        interrupts();

        --send_pixels;
        if (send_pixels > 0) {
            //Serial.printf("overrun: %d\n", send_pixels);
            subpixel_clock *= 1.01f;
            send_pixels = 0;
        }

        float clk_div = (float)clock_get_hz(clk_sys) / subpixel_clock;
        shifter_program_init(shifter_pio, shifter_sm, shifter_offset, PIN_LED_R, clk_div);

        for (int i = 0; i < frame_end_pos; i++) {
            pio_sm_put_blocking(shifter_pio, shifter_sm, test_pixels_u32[i]);
            if (send_pixels > 0) {
                break;
            }
        }
    }
}

