#include <Arduino.h>
#include <mecha.h>
#include "motori.h"

volatile int goal_fps = GOAL_FPS;
volatile float rps = GOAL_FPS;

static uint32_t tstep = 1000;
static volatile int seq_pos = 0;

static int ramp_us = KICKSTART_TSTEP;   // startup switching interval
static int ramp_us_step = 1000; // ramp up 1000us/step
static int ramp_us_min = 2000;

static bool ramp_done = false;

static float fduty = 0; // pid duty cycle
static int duty = 0;  // duty cycle used for pwm

static unsigned long start_millis = 0;    // stall detect during ramp-up
static unsigned long last_millis = 0;     // pid loop timing
static volatile unsigned long last_int_time = 0;   // hall int timing

static volatile int32_t interrupt_counter = 0; // rps computation
static volatile int32_t last_interrupt_counter = 0; // rps computation

static volatile bool step_reset = false; // hall signal to reset sequence
static volatile bool request_restart_motor = false;



// trapezoidal sequence
// U V W
static const uint8_t comm_seq[] { 
    1, 0, 0,
    1, 1, 0,
    0, 1, 0,
    0, 1, 1,
    0, 0, 1,
    1, 0, 1};

static void motor_loop();

void motori_begin()
{
    pinMode(PIN_U, OUTPUT);
    pinMode(PIN_V, OUTPUT);
    pinMode(PIN_W, OUTPUT);

    digitalWrite(PIN_U, 0);
    digitalWrite(PIN_V, 0);
    digitalWrite(PIN_W, 0);

    analogWriteFreq(20000);
    analogWriteResolution(8);

    pinMode(PIN_HALL, INPUT);

    multicore_launch_core1(motor_loop);
}

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
    last_millis = millis();
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

            if (ramp_us - ramp_us_step == ramp_us_min || interrupt_counter > 0) {
                Serial.print("ramp up complete; tstep="); Serial.println(tstep);

                ramp_done = true;
                last_millis = millis();
            }
            else {
                if (ramp_us > ramp_us_min) ramp_us -= ramp_us_step;

                if (ramp_done && (millis() - start_millis > 1000)) {
                    Serial.println("stall 1");
                    Serial.printf("ramp_done=%d\n", ramp_done);
                    restart_motor();
                }
            }
        }

        if (request_restart_motor) {
            request_restart_motor = false;
            restart_motor();
        }

        if (ramp_done && (micros() - last_int_time) > 500000) {
            Serial.printf("stall 2: rps=%f interrupt_counter=%d last=%d\n", rps, interrupt_counter, last_interrupt_counter);
            restart_motor();
            continue;
        }

        unsigned long new_millis = millis();
        constexpr float fac = 0.5f; // control loop per second
        if (new_millis - last_millis >= 1000/fac) {
            last_millis = new_millis;

            rps = (interrupt_counter - last_interrupt_counter) / 6.0f * fac;
            last_hsync_interrupt_counter = hsync_interrupt_counter;

            //if (ramp_done && rps < 2) {
            //    Serial.printf("stall 2: rps=%f interrupt_counter=%d last=%d\n", rps, interrupt_counter, last_interrupt_counter);
            //    restart_motor();
            //    continue;
            //}

            float error = rps - goal_fps;
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

            //Serial.printf("duty=%d ", duty);
            //Serial.print(" tstep="); Serial.print(tstep);
            //Serial.print(" rps="); Serial.print(rps);
            //Serial.print(" diff_us="); Serial.print(diff_us);
            //Serial.print(" hsync="); Serial.println(hsync_interrupt_counter);
            last_interrupt_counter = interrupt_counter; 
        }
    }
}

