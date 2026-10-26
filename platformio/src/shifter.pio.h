#pragma once
#include "hardware/pio.h"

// gemini-advised bit pusher

 static const uint16_t shifter_program_instructions[] = {
     0x6001, //  0: out    pins, 1         ; Shift 1 bit from OSR to the designated pin
 };

static const struct pio_program shifter_program = {
    .instructions = shifter_program_instructions,
    .length = 1,
    .origin = -1,
};

// Helper function to dynamically initialize the PIO state machine
static inline void shifter_program_init(PIO pio, uint sm, uint offset, uint pin, float div) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + 0); // Wrap the 1-line program infinitely

    // Set up the GPIO pin mapping
    sm_config_set_out_pins(&c, pin, 1);
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    // Configure the OSR (Output Shift Register)
    // true = Shift right (LSB first), true = Autopull enabled, 32 = Pull whenever OSR is empty
    sm_config_set_out_shift(&c, true, true, 32);

    // Configure the clock divider
    sm_config_set_clkdiv(&c, div);

    // Load configuration and start the state machine
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

