#include <Arduino.h>
#include <cstdint>
#include "pico/platform/common.h"

static uint32_t state = 0;
static uint32_t oldstate = 0;
static uint32_t mask = 0;

static uint64_t longpress_time;

constexpr uint64_t LONGPRESS_MS = 1000;

void btn_begin(int plus, int ok, int minus)
{
    pinMode(plus, INPUT);
    pinMode(ok, INPUT);
    pinMode(minus, INPUT);
    mask = (1ul << plus) | (1ul << ok) | (1ul << minus);
}

static int btn_status_change(uint32_t newstate, uint32_t oldstate, uint pin)
{
    uint32_t bit = 1ul << pin;

    if ((newstate & bit) == (oldstate & bit))
        return 0;
    else if ((newstate & bit) != 0 && (oldstate & bit) == 0) 
        return +1;
    else 
        return -1;
}

void btn_poll()
{
    oldstate = state;
    state = gpio_get_all() & mask;

    if ((oldstate & mask) != (state & mask)) {
        longpress_time = millis();
    }
}

bool btn_pressed(int pin)
{
    return +1 == btn_status_change(state, oldstate, pin); 
}

bool btn_released(int pin)
{
    return -1 == btn_status_change(state, oldstate, pin); 
}

bool btn_down(int pin)
{
    return (state & (1ul << pin)) != 0;
}

bool btn_longpress(int pin)
{
    bool result = btn_down(pin) && (millis() - longpress_time) > LONGPRESS_MS;
    if (result) {
        longpress_time = millis();
    }

    return result;
}
