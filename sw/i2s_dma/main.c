#include <stdint.h>

#include "bio.h" // this must always be first

// I2S mic capture that writes each sample straight into a ring buffer in shared
// memory, instead of pushing to a FIFO. The CPU reads finished samples out of
// the ring - no FIFO draining, no per-sample CPU work, so the mic clock never
// pauses between the CPU's frames. Modeled on the `i2s` program (the capture)
// and the baochip `dma_basic2` test (the store-to-memory pattern).
//
// ASSUME: quantum is set to 6.144 MHz (2 quanta per BCLK -> 3.072 MHz BCLK ->
// 64 BCLK per stereo frame -> 48 kHz).

// triangel ear mic pins - must match the schematic, the host pins.rs, and the
// `i2s` program: BCLK=PB1, SD=PB2, WS=PB3.
#define WS_PIN 3
#define SCK_PIN 1
#define SD_PIN 2

// Ring buffer size in samples (power of two). Must match the host's IFRAM
// allocation. 4096 samples at 48 kHz is ~85 ms of headroom before the CPU must
// read - well over a frame plus the mel FFT.
#define RING_N 4096
#define RING_MASK (RING_N - 1)

// Scratch "gutter" write used by dma_basic2 to clear the write pipe before real
// stores. TODO: confirm this address is valid/safe on our target before relying
// on it; it is copied from the baochip test.
#define GUTTER_ADDR 0x6101FFFC

void main(void) {
    uint32_t ws_mask = 1 << WS_PIN;
    uint32_t sck_mask = 1 << SCK_PIN;
    uint32_t sd_mask = 1 << SD_PIN;

    set_gpio_mask(ws_mask | sck_mask | sd_mask);
    set_output_pins(ws_mask | sck_mask);
    set_input_pins(sd_mask);

    // signals start at 1
    set_gpio_pins(ws_mask | sck_mask);

    // Clear the write pipe before any real store (dma_basic2 pattern).
    *(volatile uint32_t *)GUTTER_ADDR = 0;

    // The host pushes the ring buffer's physical base address on FIFO0 once at
    // startup (pop blocks until it arrives). Layout in that region:
    //   [base + 0]      head  - free-running sample write counter (we write it)
    //   [base + 4 ...]  samples[RING_N] as u32, one 24-bit left sample each
    uint32_t base = pop_fifo0();
    volatile uint32_t *head_ptr = (volatile uint32_t *)base;
    volatile uint32_t *samples = (volatile uint32_t *)(base + 4);
    uint32_t head = 0;
    *head_ptr = 0;

    int cycle_count = 0;
    uint32_t sample = 0;
    while (1) {
        wait_quantum();
        // handle ws
        if (cycle_count == 0) {
            clear_gpio_pins_n(~ws_mask);
        }
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
        if (cycle_count == 24) {
            // completed 24-bit left-channel sample -> ring buffer, then publish
            // the advanced head so the CPU knows a new sample is ready.
            samples[head & RING_MASK] = sample;
            head = head + 1;
            *head_ptr = head;
        }
        sample <<= 1;
        cycle_count ++;
        cycle_count %= 64;
    }
}
