# Display jitter result

Conclusion: REDRAW_OF_NEW_SCREEN
Root cause: the full-screen flush delays the RGB VSYNC-end ISR, and that ISR
is where the IDF 4.4 driver restarts GDMA. The delay (~420 us) crosses the
panel's restart window (the 8-line back porch, ~410 us), so the next frame
starts out of phase and part of the picture slips.
Note: none (the cursor was ruled out by E3)

## Runs

| Run | Probe line | Visual line | Class |
| --- | --- | --- | --- |
| E0 baseline | `[125.802][I][probe] PROBE screen=1 build_ms=79 flushes=25 bytes=768000 lines=800 max_flush_us=3599` | `VISUAL SWITCH SHIFT` | `FULL` |
| E1 no rebuild | `[53.621][I][probe] PROBE screen=1 build_ms=0 flushes=4 bytes=121002 lines=201 max_flush_us=3329` | `VISUAL FINGER NONE` | `SMALL` |
| E2 no SD mount | n/a | n/a | n/a |
| E3 no cursor | `[50.155][I][probe] PROBE screen=1 build_ms=76 flushes=25 bytes=768000 lines=800 max_flush_us=3686` | `VISUAL SWITCH SHIFT` | `FULL` |
| E4 no press tint | n/a | n/a | n/a |

## Mechanism measurement

Timing of this project's RGB panel: pclk 16 MHz, h_total 820, v_total 500.
One frame is 25.625 ms; the vertical blank is 20 lines (~1.025 ms) and the
back porch after VSYNC-end is 8 lines (~410 us).

A probe in the `on_frame_trans_done` callback timestamped every VSYNC-end
ISR (the RGB driver restarts GDMA from that ISR). Measured over 15 screen
changes:

| Case | ISR-to-ISR interval | In a flush? |
| --- | --- | --- |
| idle baseline | ~25,673 us | no |
| full rebuild, no visible glitch | ~25,677 us | no |
| full rebuild, visible `SHIFT` | 26,046 us (+373 us) | `in_flush=1` |
| largest logged spike | 26,093 us (+423 us) | `in_flush=1` |

Every significant spike occurred on a frame with a `draw_bitmap` in flight;
frames without a copy stayed at baseline. The clean single tap that produced
`SHIFT` measured 26,046 us with `in_flush=1`.

To prove that a delayed restart alone is sufficient, one VSYNC-end restart
was delayed by a controlled amount inside the callback:

| Injected one-frame delay | Artifacts in ~20 s |
| --- | --- |
| 350 us | 0 |
| 400 us | 2 (part of the image jumped down) |

A continuous 400 us delay produced only a brightness flash and no slip,
because the phase settles; the transient single-frame delay is what slips the
picture. The natural delay (~420 us including the ~48 us baseline) sits in the
same band, which matches the intermittent occurrence (some taps `SHIFT`, some
`NONE`).

## What this separates

- The glitch is not the cursor (E3) and not the press tint.
- It requires the full rebuild: E1 skipped the rebuild (121,002 bytes instead
  of 768,000) and the screen-change `SWITCH` glitch disappeared.
- The flush's per-strip `memcpy` + `Cache_WriteBack_Addr` into the single live
  PSRAM framebuffer holds off the VSYNC-end ISR. The restart therefore lands
  at or past the end of the 8-line back porch, and the next frame is out of
  phase. The effective window is ~400-450 us, not the full ~1 ms blank, so
  the event is sporadic.
- The failure needs the single-framebuffer design (no second buffer, no
  vsync-synchronised copy) and the bulk writeback during scanout.

## Not done

No fix was applied. The four source files were restored and the device was
reflashed with the restored firmware.
