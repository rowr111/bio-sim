# i2s_dma

I2S microphone capture, writing samples straight into
a ring buffer in shared memory (IFRAM) instead of pushing them to a FIFO. The CPU
reads finished samples out of the ring, so it never has to service a FIFO per
sample and the mic's clock never pauses between the CPU's frames.

It is the original `i2s` capture loop, unchanged, with the `push_fifo0(sample)`
replaced by a store into the ring (the pattern from the baochip `dma_basic2`
test). Pins are [the triangel ear's](https://github.com/rowr111/triangel/tree/main/firmware/ear): BCLK=PB1, SCK_PIN=1; SD=PB2, SD_PIN=2;
WS=PB3, WS_PIN=3.

## Ring buffer contract (host <-> BIO)

- Host pushes the ring's physical base address on FIFO0 once at startup.
- Layout at that base: `[base+0]` = `head` (free-running sample counter, written
  by the BIO); `[base+4 ...]` = `samples[RING_N]` as u32, one 24-bit left sample
  each. `RING_N` = 4096 (must match the host allocation).
- CPU reads `head`, copies `tail..head`, advances `tail`; a `cache_flush` is
  needed before the CPU read (see the `dma_basic2` test).

## Testing

The block-level bio-sim harness currently ties off the DMA bus
(`benign_dma_inputs` in `sim/sim_main.cpp`: `aw_ready/w_ready=1` but
`b_valid=0`), so the BIO's stores don't land anywhere and would stall. Exercising
the memory path in sim needs a small AXI memory slave added to the harness (store
writes into a backing array, return `b_valid`; serve reads with `r_valid`/
`r_data`) plus a command to read that array back. The capture timing itself is
identical to `i2s`, which the sim already validates.
