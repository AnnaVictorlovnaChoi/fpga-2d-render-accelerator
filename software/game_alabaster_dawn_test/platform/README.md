# Local software platform for v9 GPU

V9 target: F:/Efinity/project_2026fpga/v9_Ti60F225_demo_new_7cache_HDMI. Register/API compatibility was audited against actual RTL; v9 has seven 1024-pixel RGB565 cache slots (14KiB total). Application deliberately keeps cache=0. See ../docs/v9_baseline.md and ../docs/v9_baseline.json. This preserves the inherited software snapshot rather than replacing it with the new demo's weaker commented-out validation. No hardware RTL or bitstream was changed. Hardware XML contains external D: source paths, so F: source/loaded-bitstream identity is not proven.

## Inherited software provenance

## Provenance and scope

Source root: `F:/Efinity/project_2026fpga/v8_Ti60F225_demo_new_AXIupdate/Ti60F225_demo_new/par/ddr_demo_ti60/embedded_sw/soc`.

- `gpu/2D_DMA.c`, `gpu/2D_DMA.h`, `gpu/userDef.h`: byte-identical HDMI_Stress/src snapshot. No driver fixes, added guard, retry or delay.
- `common/`, `driver/`: entire v8 software/standalone directories.
- `bsp/include/`: entire v8 bsp/efinix/EfxSapphireSoc/include directory. Unused lwIP/FATFS apps are not required or imported.
- v8 makefiles mention standalone/include, but that directory does not exist in this baseline. No dependency on it or v4 exists here.
- `hdmi_ddr.ld`: derived from Stress linker; only RAM length becomes 95M and BSS/stack explicitly NOLOAD. ORIGIN remains 0x1000, end 0x05F01000, below first framebuffer 0x06000000. BIN has an independent <=2MiB check; large zero-initialized runtime buffers must be BSS, never initialized arrays.
- `v8_platform.h`: aggregation header and reference timing constants, no runtime wrapper.
- `snapshot_manifest.json`: hashes and exact source provenance for copied files. Linker is separately identified as adapted.

No original files or G2.1 files were changed. Driver selection follows H0.1 audit: Stress preserves poll/re-arm and faster unvalidated raw bitblt submission. Application must validate dimensions, frame IDs and buffers itself. The original declared `frame_switch()` has no implementation; do NOT call it. No replacement semantics were invented.

## API and performance

Include `v8_platform.h` or the original `2D_DMA.h`. `pic_blt` and `pic_sprite` arguments are `(base, src_w, src_h, width, height, sx, sy, dx, dy, frame, cachein, cachenum)`. `pic_alpha` and `pic_alphasprite` insert float alpha immediately after frame. `pic_colorfill(width,height,dx,dy,frame,color565)` has no source. `cachein` is bit31 and cache slot is `(cachenum & 7)` in bits30:28; these do not clean CPU D-cache.

- G2.3 software CLINT is 300000000 Hz, matching the user-updated 300MHz CPU hardware; DCache16KiB/ICache8KiB are hardware settings, not software cache-allocation changes.
- Copy/fill successful submissions have no per-command delay.
- Application retains 24x24 sprite 4us reference and requested 32x32 14us reference, including cache priming where applicable; these are not silently added to every GPU call. Stress demo source actually used 7us and old assembly 11us; neither is relabeled 14us. Alpha timing is not established by these demos.
- `bitblt()`/pic return 0 means command sent, not finished. Send -2 is FIFO rejection. `bitblt_wait` -2 is hardware ERROR, not retryable FIFO backpressure; -1 is timeout in poll counts.
- No frame-tail 60us delay, new empirical guard or fake firmware is provided.

## Application frame ownership contract (not implemented by this snapshot)

The raw state machine is unsafe if treated as a queue: acquire does not reserve, submit replaces any pending frame, and ISR-shared internal ints are not volatile. Keep one CPU producer and one rendering frame; do not enable LTO or inline raw state access into spinning loops.

1. Application tracks DISPLAYING, RENDERING and PENDING independently. Strong override of weak `frame_pipeline_tick_isr()` increments a volatile sequence only (no UART). Driver calls it after its pending handoff. Call `frame_pipeline_poll()` regularly to re-arm low level.
2. Call raw acquire and submit in **short interrupt-masked sections**, with compiler memory barriers; restore prior MIE, not unconditional enable. Never wait or render under the mask. Maintain an application pending flag. At submit, atomically sample tick, submit once, and publish pending flag/frame before restoring interrupts. Reject another submit while that flag remains set.
3. Only clear application pending after a tick different from the captured submit tick, observed atomically. With no other submitter this means the driver had the chance to consume that pending selection; it does NOT independently prove HDMI latched/displayed it.
4. Finish GPU writes before submit. Use `bitblt_wait`, or an equivalent bounded nonblocking ERROR/DONE/!BUSY status machine while continuing poll; FIFO acceptance or a timed delay is not completion. Check the v8 hardware DONE/FIFO/DDR completion assumptions described by H0.1; this unmodified software alone cannot prove them.
5. Avoid immediate reuse of the old playing buffer when software updates playing at an IRQ. Conservatively rotate 1/2/3 with an application-owned retired-frame tick and only reuse after the next valid handoff tick. Initial playing=1; begin rendering another buffer. Never draw to currently selected or pending frame; do not clear all three after enabling display. If tick/complete is lost, stop/timeout rather than guessing availability.
6. `frame_pipeline_init` selects frame1 before any memory clear, so application must initialize visible memory before enabling the pipeline if startup black is required. The GPIO frame-number bits alone are not a proven physical display acknowledgment.

This ownership discipline avoids software pending overwrite and early software reuse; no guarantee of tear-free scanout is claimed without board verification of handoff timing. CPU-written textures also require confirming actual D-cache/DDR coherence; GPU cache slots are unrelated.

## Build contract

From project root run `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 -CheckOnly` for local dependency/compiler checks, then `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1` after all src and asset inputs exist. Windows PowerShell 5 is supported; PowerShell 7 (`pwsh`) is optional, not required. Set `RISCV_TOOLCHAIN_BIN` or pass `-ToolchainBin` to override the tool directory; PATH and the installed F:/Efinity toolchain are fallback choices. Python is configurable with `-Python`.

All src/*.c and src/*.S recursively compile; local GPU, start.S, trap.S and syscalls.c are explicit. Assembly .incbin resolves relative to project root. RV32IM_Zicsr_Zifencei, ilp32, O2, no LTO, local includes only. A complete rebuild avoids stale-header objects; object paths retain directory names. The runtime RAM window is 95MiB and the BIN load budget is separately <=2097152 bytes. Python emits raw uppercase two-digit byte lines with LF, not Intel HEX, streaming 64KiB chunks without PowerShell byte objects. Builds use unique run directories; only successful complete builds update build/latest-success.txt. No dummy main or placeholder assets are synthesized.
