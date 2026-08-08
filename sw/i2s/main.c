#include <stdint.h>

#include "bio.h" // this must always be first

// ASSUME: quantum is set to (3.072 MHz) 6.144MHz

// Pins match the triangel controller board.
#define SCK_PIN 1
#define SD_PIN 2
#define WS_PIN 3

// Host configures FIFO0 slot 0 to fire while the level is under 7 = room to push.
#define FIFO0_HAS_ROOM FIFO_EVENT_MASK(0, 0)
// x30 halts until (x27 & events) is nonzero but returns the unmasked value, so a
// permanently-set bit in the mask makes the read non-blocking.
#define ALWAYS 1

void main(void) {
    uint32_t ws_mask = 1 << WS_PIN;
    uint32_t sck_mask = 1 << SCK_PIN;
    uint32_t sd_mask = 1 << SD_PIN;

    set_gpio_mask(ws_mask | sck_mask | sd_mask);
    set_output_pins(ws_mask | sck_mask);
    set_input_pins(sd_mask);

    // signals start at 1
    set_gpio_pins(ws_mask | sck_mask);

    set_event_mask(ALWAYS | FIFO0_HAS_ROOM);
    set_event_bits(ALWAYS);

    int cycle_count = 0;
    uint32_t sample = 0;
    while(1) {
        wait_quantum();
        // handle ws
        if (cycle_count == 0) {
            clear_gpio_pins_n(~ws_mask);
        }
        // The mic drives from cycle 1, a cycle after WS drops.
        if (cycle_count == 1) {
            sample = 0;
        }
        if (cycle_count == 32) {
            set_gpio_pins(ws_mask);
        }
        clear_gpio_pins_n(~sck_mask);

        wait_quantum();
        if (read_gpio_pins() & sd_mask) {
            sample |= 1;
        }
        set_gpio_pins(sck_mask);
        // 24 bits, cycles 1..24. Never block: pushing into a full FIFO halts the
        // core, which stops BCLK and puts the mic to sleep. Drop the sample instead.
        if (cycle_count == 24 && (event_status() & FIFO0_HAS_ROOM)) {
            push_fifo0(sample);
        }
        sample <<= 1;
        cycle_count ++;
        cycle_count %= 64;
    }
}
