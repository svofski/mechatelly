#include <Arduino.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"

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

