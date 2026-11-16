# HDMI Frame Switch Debug Note

> 工程根目录固定为 `D:\26FPGA\Ti60F225_demo_new`。所有 RTL、软件、约束、综合/布局布线报告和生成结果均以此目录为准；不得使用或引用 `D:\26FPGA\Ti60F225_soc_demo` 作为当前工程目录。

## Per-core burst prefill decoupled from AXI ownership (2026-10-25)
- Changed the production `rtl/GPU/axi_rgb565_writer.v` / `rtl/GPU/axi_rgb565_dma.v` path so each per-core writer accepts its parser row command while the shared AXI owner is unavailable, packs one complete burst into its existing local beat buffer, and raises `bus_request` only when that burst is ready.
- The DMA now arbitrates writes from `bus_request`; it grants the writer by owner ID, and releases that owner on each burst's B-response completion (`burst_done`), not only when the whole row finishes. The writer retains the row command and pre-fills its next burst after releasing the bus. Parser `write_done` semantics remain row-level and are still asserted only after the final burst B response.
- A writer's local input ready is independent of AXI owner. This lets copy/sprite/alpha/scaler output packing proceed while another core owns AXI; it does not create concurrent AXI transfers, and a single active core still performs the same input packing work. First version uses one burst buffer per core; it does not overlap transmission and next-burst fill for that same core.
- Validation: ModelSim syntax compile passed for writer, 8-core DMA, parser and scheduler. Added `verification/rtl/tb_axi_rgb565_writer_preload.sv`; simulation passed a 128-byte row as two bursts, asserting no AW/W before grant and checking two grants, two AWs, 32 W beats and final row completion. Integrated FPGA timing and on-board rendering remain to be checked.

## Frame-switch timing re-estimate (2026-10-24)
- Current target is 720p60 with an active area of 1280 x 720 and timing totals of 1650 x 750. The pixel clock for this timing is 74.25 MHz; the `148.5 MHz` text still present on the top-level `clk_hdmi` port is a stale comment and must not be used as the pixel-clock value. `clk_hdmix5`/TMDS-related clocks are separate.
- One line is `1650 / 74.25 MHz = 22.222 us`; one frame is `1650 * 750 / 74.25 MHz = 16.667 ms` (60 Hz). The active-low VSYNC interval is 5 lines, so the interval from the user-clock interrupt edge (VSYNC falling edge after synchronization) to the HDMI-domain sample at VSYNC rising edge is approximately `5 * 22.222 us = 111.111 us`, minus only the small synchronizer phase uncertainty.
- CPU/GPU/user clock is currently 200 MHz, so one clock is 5 ns. The RTL synchronizers add roughly 2-4 destination-clock cycles (about 10-20 ns at 200 MHz, plus clock-phase uncertainty). The RISC-V path `trap_entry -> trap -> plic_claim -> frame_switch_isr -> gpio_setOutput` is normally on the order of hundreds of CPU cycles to a few thousand cycles, approximately 1-10 us at 200 MHz. Even a conservative tens-of-microseconds software response remains below the 111 us hardware window.
- `gpio_frame_switch_count <= 17'd100000` keeps GPIO1[0] high for `100000 / 200 MHz = 500 us`. This is intentionally much longer than the 111 us sampling window; it guarantees that the HDMI-side synchronizer sees the level, but it also means GPIO1[0] is a level-held interrupt rather than a narrow pulse. Whether this causes repeated PLIC service depends on the GPIO interrupt mode (edge versus level) and should be checked separately.
- Therefore the current frame-switch timing has sufficient margin for software to write the selected frame before the HDMI-domain VSYNC-rising sample. A missed frame caused solely by CPU interrupt latency is unlikely at 200 MHz; remaining issues should be investigated in the GPIO interrupt mode, frame-request synchronization, blanking/drain state machine, or framebuffer/DMA completion ordering.

> 2026-10-23 当前 writer 开发计划与逐周期说明见 [AXI_writer_plan.md](AXI_writer_plan.md)。本轮在用户确认稳定的字节版上，仅增加 FILL 缓存像素的 16 位处理分支；保留接收缓存拍和原字节兼容分支。16 组测试及 GPU RTL 编译通过；新实现时序和上板待验证。下方为历史记录，不代表全部仍在当前源码中。

## State
- User Interrupt A now reaches the CPU. The expected path is `trap()` -> `CAUSE_MACHINE_EXTERNAL` -> `plic_claim()` -> source 16 -> frame ISR.
- Current symptom is intermittent whole-frame corruption/tearing, including horizontal noisy bands and mixed image regions. It persists with long software delays, so delay alone does not solve it.

## Confirmed Parameters
- VGA active area: 1280 x 720; timing totals are 1650 x 750.
- RGB565 frame size: `1280 * 720 * 2 = 0x001c2000` bytes.
- Software framebuffer bases: `0x06000000`, `0x06200000`, `0x06400000`; spacing is 2 MiB.
- Software stride: 2560 bytes.
- HDMI FIFO writes 32-bit words and reads 16-bit pixels. `rd_datacount_o` is in read-side 16-bit words; 1280 means one complete line.

## Main Timing Bug Found
- Software previously treated a submitted frame as displayed immediately after writing GPIO.
- HDMI samples the GPIO request at the next VSYNC. The user-clock state machine then drains the AXI burst, resets the async FIFO, selects `now_frame`, and waits for FIFO reset completion.
- During that interval the old/requested buffer must remain reserved. Reusing it can make the DDR reader and GPU write different parts of the same frame, producing the observed full-frame corruption.

## Current Source Changes
- Alpha `2D_DMA.c` keeps `frame_playing` as the hardware-confirmed display frame.
- `frame_request_frame` reserves the GPIO request until the ISR sees the RTL next-writable-frame status.
- `frame_pipeline_acquire()` atomically reserves a render target and excludes display, request, latest, and current render frames.
- RTL reset value of `frame_next_user` is 2 because `now_frame=0` corresponds to software frame 1 being displayed and software frame 2 being writable.
- RTL raises User Interrupt A on each synchronized active-low VSYNC falling edge.

## Validation
- Rebuild Alpha software and regenerate/download the FPGA bitstream before testing.
- Check that the ISR GPIO status cycles through next-writable values 1/2/3 and that `frame_playing` follows 3/1/2 respectively.
- If corruption remains, capture `frame_playing`, `frame_request_frame`, `frame_new`, `frame_rendering`, `gpio`, and IRQ sequence at every ISR; compare them with `now_frame`, `pingpong_state`, `rst_busy`, `rd_datacount_o`, `empty_o`, and `underflow_o` in RTL.
- No RISC-V/Verilog compiler was available in this shell; static source checks only. Preserve unrelated worktree changes.

## AXI Writer Burst-Buffer Update (2026-10-23)
- Reworked `rtl/GPU/axi_rgb565_writer.v` to stage each complete write burst in a local beat buffer before asserting `AWVALID`; `WVALID` is only asserted after `AW` handshake. This restores the old RTL's prepare-then-send transaction ordering while preserving the current fill-mode interface and RGB565 pixel packing.
- Kept OP=4 on the existing `fifo_valid/fifo_ready/fifo_data` interface. The scaler and scale FIFO connections were not changed.
- Delayed `axi_rgb565_dma8` writer-owner release by one cycle after the writer `done`/`error` pulse, so the row parser can observe completion before the next row is granted.
- ModelSim writer checks pass for alignment offsets, 4KB crossing split, stalled WREADY, a 720-pixel ordinary FIFO row, and a 720x480 RGB565 fill (345600 pixels). `axi_rgb565_dma.v` and its reader/FIFO dependencies compile successfully.
- Board validation remains required for the HDMI flicker and stress partial-fill symptoms.

## OP=4 Parser Length Timing Revert (2026-10-23)
- User reported continued display jitter with the legacy AXI RTL and asked to investigate parser logic only.
- Compared `bitblt_cmd_parser.v` against `D:\26FPGA\720\rtl\GPU\bitblt_cmd_parser.v`. The newer parser had moved the OP=4 source-row byte-length calculation from command acceptance (`ST_IDLE`) to `ST_ISSUE`, using a newly added `scale_width_product_reg` and updating the length on every `ST_ISSUE` cycle.
- Restored the legacy behavior: compute and latch `scale_read_length_reg` once when accepting the OP=4 command, and removed the intermediate product register and issue-state rewrite. OP=4 row-Y/address logic and interfaces were not changed.
- Added `verification/rtl/tb_bitblt_cmd_parser_scale.sv`; ModelSim passed widths/scales `(720,64)->1440 bytes`, `(720,32)->720 bytes`, `(321,96)->962 bytes`, and verified read length remains stable while `read_cmd_valid` is stalled.
- Parser/scheduler compile passed. The writer testbench also passed unchanged. This is a focused parser timing/behavior revert, not a confirmed explanation of the displayed jitter; board testing is still required.

## Pixel packing only, after user rollback (2026-10-23)
- Current project XML selects `rtl/GPU/axi_rgb565_writer.v`. Only this production module was changed for this step; parser, DMA arbitration and frame switching were left untouched.
- Writer consumes one RGB565 pixel per ready/valid handshake, combines two pixels per full beat, and uses 1100/0011 strobes for halfword head/tail. Retains PREP/FILL/AW/W/B sequencing and one outstanding burst. Requires even byte address/length; odd pixel counts are supported.
- Updated writer testbench for the rollback interface. Ten memory-scoreboard cases passed, including offset 2, odd pixel counts, 4KB boundary, FIFO gaps, AW/W stalls and a full 720x480 fill. Board behavior and post-change timing still require implementation testing.
- Eight-core DMA holds a single owner throughout each row write including packing and B waits. At 16 beats/burst, aligned 32-pixel burst costs ideally 1 PREP + 32 FILL + 1 AW + 16 W + 1 B = 51 cycles, versus 115 with old three-cycle-per-pixel packing. These exclude command/arbitration overhead and external stalls; 200 MHz gives 255 ns versus 575 ns. No fill bypass or packing/transmission overlap was added.

## Revert pixel packing after board regression (2026-10-23)
- User observed flicker after the packed writer was programmed. Restored rtl/GPU/axi_rgb565_writer.v from D:/26FPGA/720/rtl/GPU/axi_rgb565_writer.v (the unmodified byte-serializer baseline); no other production RTL was changed.
- The writer transaction testbench passes against this baseline, including odd pixel count, offset 2, 4KB boundary, source gaps, AXI stalls and 720x480 transfer. Board validation is needed to confirm flicker is gone.
- The current timing report shows core_clk at 199.800 MHz with -0.005 ns slack. Its worst same-clock path is sched_wh[3] to PARSER[5].u_parser/scale_read_length_reg[27] through multiplier logic, not an AXI writer endpoint. This report alone does not establish whether the writer contributed to the small placement/timing regression; re-run implementation after writer restore for a valid comparison.

## Reader/Writer ModelSim Comparison (2026-10-28)
- Added `sim/reader_writer_compare/tb_reader_writer_compare.v` and `run.do`. The testbench uses the current `axi_rgb565_reader.v` and `axi_rgb565_writer.v`, a 200 MHz clock, one aligned 64-byte RGB565 row (32 pixels), continuous AXI `R`/`W` handshakes, and counters for command, burst, data and completion events.
- Baseline with truly back-to-back read data and no B delay:
  - Reader: 19 cycles from command acceptance through completion, 1 AR burst, 16 accepted R beats, no error.
  - Writer: 85 cycles, 32 FIFO pixel handshakes for FILL, 1 AW, 16 W beats and 1 B response.
  - The writer is longer in this ideal model because the current writer serially stages 32 RGB565 pixels before AW; the reader writes each 32-bit AXI beat directly to the 32->16 FIFO.
- Reader initial AXI return latency sweep (latency is inserted before the first R beat; R beats remain continuous): `0 -> 19 cycles`, `2 -> 21`, `4 -> 23`, `32 -> 51`, `64 -> 83`, `128 -> 147`, `192 -> 211`. Writer B response sweep: `B delay 0 -> 85 cycles`, `2 -> 87`, `4 -> 89`.
- A reader wait of roughly 228 cycles per 64-byte row would make the modeled reader about `247 cycles`, or 2.9 times the 85-cycle writer row. Therefore the measured BLT/colorfill ratios cannot be caused by an old 32-bit-to-two-16-bit drain in the current reader; they require substantial DDR read response/turnaround time, inter-beat gaps, AXI arbitration delay, or FIFO-full backpressure.
- The testbench does not model the real DDR controller, multiple cores, AXI owner arbitration, or FIFO saturation. It proves only the local engine timing under explicit AXI response assumptions. The next diagnostic should capture/measure `AR handshake -> first R`, gaps between `R` handshakes, `RREADY` stalls, and owner grant/yield cycles on the integrated design.
- Testbench correction: the first version deasserted `RVALID` for one cycle after each accepted beat. Fixed the responder to keep `RVALID` asserted across accepted beats; use the corrected numbers above. The local reader's 16 R beats are now genuinely back-to-back in the zero-latency case.

## Reader Slowdown Investigation (2026-10-28)
- `axi_rgb565_reader.v` does not insert gaps between R beats by itself. During `ST_R`, `m_axi_rready` is high whenever `fifo_full` is low, and each `RVALID && RREADY` beat is written directly to the 32-bit FIFO. Any first-beat latency or inter-beat gap therefore comes from the DDR/AXI controller or an upstream stall, not the reader's pixel unpacking.
- The reader does release the shared DMA owner at every completed burst: the final R beat moves the reader to `ST_YIELD`, `bus_yield` is asserted, and `axi_rgb565_dma.v` sets `owner` to `NONE`. The next burst then needs a new arbitration grant, an AR handshake, and DDR return. A 64-byte row is one 16-beat burst, so a 32-row BLT repeats this owner/AR turnaround 32 times. The arbiter scans source readers first, destination readers second, and writers last; this can delay writes while any reader request remains active.
- FIFO backpressure is ruled out as a cause for one 32x32 command under the current row-at-a-time parser: source and destination readers each request only one 64-byte row (32 pixels), and the parser cannot advance to the next row until that row's write completes. Each per-core 2048-pixel FIFO therefore cannot reach `full` at count 2047 or `prog_full` at 1984 during this test. The `RREADY` full gate matters for larger/in-flight workloads, but not for this 32x32 case.
- The DDR controller's `axi4_bus_ctl.v` is protected/encrypted and the downstream register-slice/DDR path is not observable as plain RTL timing. Therefore the actual AR-to-first-R latency and gaps between R handshakes cannot be confirmed from source alone. Integrated simulation or ILA counters are required: count cycles from `m_arvalid && m_arready` to first `m_rvalid && m_rready`, count gaps between accepted R beats, and count cycles where `m_axi_rvalid && !m_axi_rready`.
- Current source configuration needs verification before comparing core counts: `GPU_top.v` declares `CORE_COUNT=2` by default, while the `ddr3_soc_top.v` instance shown in this project passes burst parameters but no `CORE_COUNT` override. Unless the synthesis top or generated wrapper overrides it elsewhere, this RTL elaborates two GPU cores, not six or eight. The implementation log/elaborated hierarchy must confirm the actual count.
- Current status: owner/burst turnaround is confirmed; reader-created R gaps are ruled out by local RTL; FIFO-full stalls are ruled out for one 32x32 command by the row size and FIFO depth; DDR response latency and inter-beat gaps remain unmeasured because the controller is protected. Do not attribute the measured 3.9x BLT/colorfill ratio solely to reader unpacking until integrated AXI counters are collected.

## Reader AXI Event Counters (2026-10-28)
- Extended `sim/reader_writer_compare/tb_reader_writer_compare.v` to measure the requested reader events over a 2048-byte request (32 x 16-beat bursts): AR handshake to first accepted R beat, maximum gap between accepted R beats, cycles with `RVALID && !RREADY`, and reader yield to next AR handshake.
- Under the zero-delay, back-to-back R responder: `AR->first R = 2 cycles` for every burst (average/max 2); max gap between accepted R beats = 0; `RVALID && !RREADY = 0`; 32 bursts were accepted. The reader-only `bus_yield -> next AR handshake` interval is 1 cycle because this testbench does not instantiate the DMA owner arbiter.
- With a modeled 32-cycle delay before the first R beat of each burst: `AR->first R = 34 cycles` average/max; max R gap = 0; `RVALID && !RREADY = 0`; 32 bursts. This demonstrates that first-beat latency dominates when configured, while the responder keeps data beats continuous.
- Integrated owner turnaround derived from `axi_rgb565_dma.v`: after `bus_yield`, the owner register is cleared on the next clock; with no competing request, grant is captured on the following clock and AR can handshake one clock later. Thus owner-release to next AR handshake is 2 cycles uncontended; measured from `bus_yield` assertion to handshake it is 3 cycles. Competing reader requests can extend this by arbitration wait; the current unit test does not model contention.
- These are ModelSim measurements for explicit testbench responder timing, not real DDR observations. The actual board's `AR->first R` and R beat gap remain unknown until the DDR model is included in simulation or the integrated signals are counted with ILA. `RVALID && !RREADY` is structurally impossible in the no-full test because RREADY stays high; 32x32 row-at-a-time traffic also cannot fill the 2048-pixel FIFO.

## MT41J128M16JT-125 Reader Bottleneck Analysis (2026-10-28)
- The supplied datasheet identifies `-125` as DDR3-1600: `tCK=1.25 ns`, `CL=11`, and target `tRCD=tRP=13.75 ns`. The device is x16; the board/controller must combine the required devices to form the controller data width. Raw chip bandwidth is not the same as the GPU AXI bandwidth.
- A row-hit READ has a DRAM-side CAS delay of about `CL*tCK = 13.75 ns`. A row-miss READ adds `tRCD` before the READ, so the DRAM command-side minimum is roughly `tRCD + CL = 27.5 ns`, before the controller's command queue, PHY, AXI register slices and user-clock response latency. At 200 MHz this corresponds to about 2.75 or 5.5 user-clock periods as a pure time conversion; the protected controller can add more cycles.
- The current reader already uses a 16-beat AXI burst and writes each 32-bit R beat directly to the 32->16 FIFO. The local reader test measured 19 cycles for a 64-byte row with back-to-back R data, so pixel packing is not the dominant cost. The reader's extra cost is the controller-visible AR-to-first-R delay and any gaps after that first beat.
- The largest RTL amplification is transaction scheduling: `bitblt_cmd_parser.v` waits for a row's reader completion before advancing, and for sprite/alpha it waits for both source and destination readers before issuing the writer. `axi_rgb565_dma.v` has one global `owner`; each reader burst ends in `ST_YIELD`, clears owner, then requires a new grant and AR handshake. There is no read request queue or multiple outstanding read burst in the GPU reader. Thus the design repeatedly pays read startup and owner arbitration for every row.
- Writer is more tolerant of this latency because it fills its local burst buffer before requesting owner. Its FILL can overlap with another core's AXI activity. Reader has no equivalent prefetch: it cannot fill the source FIFO until DDR returns data, and the parser does not issue the next row until the current row completes.
- Optimization priority: (1) keep the current 16-beat burst and add a small per-core read prefetch/descriptor queue so the next row AR can be accepted while the current row is being consumed; (2) retain at least one outstanding read burst per core, with response routing by ID; (3) allow the parser to prepare/issue the next row while the writer drains the current row, using FIFO occupancy as the dependency; (4) reduce `ST_YIELD`/owner turnaround by retaining owner for a queued next burst or using a burst grant quantum; (5) only then consider longer bursts or DDR scheduling changes. Larger bursts cannot remove the first-read latency if the parser still waits row by row.
- For sprite/alpha, source and destination readers currently compete serially for the same owner. A two-entry read queue or a scheduler that accepts both AR requests before their R responses would hide one reader's startup latency. This is the main architectural path toward making reader throughput approach writer throughput.

## Reader one-burst AR prefetch (2026-10-28)
- Implemented in `rtl/GPU/axi_rgb565_reader.v`: while the current burst is returning `R`, the reader computes and queues one descriptor for the following burst (address and beat count). The following `AR` can handshake before the current burst reaches `RLAST`. AXI response order remains the FIFO order; the design keeps the existing fixed ID and does not introduce response reordering.
- The reader keeps the shared owner across burst boundaries. `bus_yield` is now asserted only when `pause_new_bursts` requests a stop or when the whole command finishes; the DMA's existing owner logic therefore no longer releases/re-arbitrates between ordinary bursts. Parser row and OP=4 semantics are unchanged.
- The queued descriptor is promoted only after the current burst's `RLAST`. A one-entry `prefetch_sent` guard prevents duplicate `AR` requests. FIFO `keep`, `last`, `RRESP`, and `RLAST` checks remain active.
- ModelSim regression: `sim/reader_writer_compare/run.do` compiles reader/writer and runs successfully. For a 2048-byte request (32 x 16-beat bursts), the corrected responder reports 32 AR handshakes and no `RVALID && !RREADY` stalls. With zero modeled first-R delay the reader took about 546 cycles; with a 32-cycle first-R delay per accepted AR it took about 848 cycles. These numbers are a transaction-model comparison, not a board DDR measurement; the test responder now permits one queued AR while the current R burst is active.
- Full RTL syntax compilation of reader, writer, `axi_rgb565_dma8`, and `bitblt_cmd_parser8` passed with ModelSim. The remaining architectural limit is one queued burst per reader and one global AXI owner; source and destination readers are still serialized by the DMA arbiter.

## SoC AXI outstanding-read capability (2026-10-29)
- The visible SoC AXI upsizer is `rtl/ddr3_controller/axi4_bus_ctl/axi_upsizer.v`. Its `C_SINGLE_THREAD` parameter defaults to `1`; the source comment explicitly says multiple outstanding transactions are allowed when their IDs are equal, while a different ID is stalled until the queue drains.
- The same upsizer sets `C_FIFO_DEPTH_LOG = 5`, so its command FIFO is nominally 32 entries. `allow_new_cmd` is gated by `cmd_full`, and the queue stores accepted AR/AW command metadata before downstream DDR service. The AR and R register slices are enabled/lightweight in the visible configuration.
- GPU reader drives a fixed `m_axi_arid = 0`, so consecutive AR requests from one reader satisfy the same-ID condition. A one-entry reader prefetch can therefore be supported by the SoC AXI IP; the necessary ownership rule is that the DMA keeps one `read_owner` until all queued bursts for that parser finish. The current reader rollback that forbids AR during R is a correctness isolation step, not an AXI-IP requirement.
- The DDR controller implementation behind `axi4_bus_ctl.v`/`ddr3_top.v` is protected, so the exact physical command acceptance depth and DDR scheduling cannot be proven from the encrypted source. `ARREADY` proves acceptance into the visible AXI layer, not necessarily immediate DDR command execution.

## HDMI_Stress colorfill accepted=0 diagnosis (2026-10-29)
- `HDMI_Stress/src/2D_DMA.c:214-216` returns `-2` before writing any of the seven command words whenever `GPU_FIFO_STATUS_REG & GPU_STATUS_PROG_FULL` is set. `HDMI_show.c` increments `fills_this_frame` only when `pic_colorfill()` returns `0`, so `accepted=0` means commands were rejected at this software FIFO gate; it does not mean the writer completed zero bursts.
- Colorfill bypasses both source readers, but it still depends on the command AXI slave/FIFO, scheduler, color parser, writer command handshake, and writer completion. A reader deadlock cannot directly consume colorfill data, but a reader/parser reset or timing failure can leave the shared scheduler/parser or command FIFO stuck, making `PROG_FULL` remain asserted and causing every later colorfill submission to return `-2`.
- To distinguish causes on hardware, print the return value and FIFO status on the first colorfill attempt. If the result is `-2` with `PROG_FULL=1`, inspect command FIFO drain/scheduler busy. If result is `0` but `bitblt_wait()` times out, the command was accepted and the failure is downstream in parser/writer/AXI completion. This distinction is required before attributing accepted=0 to the writer alone.

## Colorfill parser deadlock found and fixed (2026-10-29)
- Root cause was in `rtl/GPU/bitblt_cmd_parser.v`, `ST_WAIT`, `operation == 3'd1`: colorfill unconditionally incremented `row_index` and returned to `ST_COLOR` after every completed write row. It never checked `row_index + 1 >= height_pixels`, so every colorfill command looped forever, never asserted `bitblt_done`, and eventually left the command FIFO at `PROG_FULL`. This explains `HDMI_Stress accepted=0` even though colorfill does not use reader, and also explains later HDMI_Alpha stalls after earlier colorfill work.
- Fixed the colorfill branch to assert `bitblt_done`, clear `bitblt_busy`, and return to `ST_IDLE` on the final row; intermediate rows retain the old address/row advance behavior.
- Added `sim/parser_colorfill/tb_parser_colorfill.v`. With two parser instances, a mock writer, and 12 queued colorfill commands, the fixed path produced 12 writer command accepts, 12 parser done pulses, zero FIFO words remaining, no parser errors, and both parsers idle. Before the fix, writer accepts grew continuously, parser done stayed zero, and the FIFO remained non-empty.
- Also fixed `rtl/GPU/gpu_sync_fifo.v`: equal-width FIFOs previously elaborated an invalid reversed part-select (`mem[31:32]`) behind a constant conditional. The new generate branches use a direct equal-width read and preserve the 32-to-16 lane path. This was required for ModelSim elaboration and avoids simulator/synthesis-dependent behavior in the command FIFO.
- After fixing colorfill completion, restored the reader's one-entry AR prefetch in `rtl/GPU/axi_rgb565_reader.v`: while a burst returns R data, the reader may submit one following descriptor, stores its metadata, and promotes it after current RLAST. The DMA retains the same read owner through the command; all requests continue to use AXI ID 0.
- The reader/writer ModelSim regression passes after prefetch restoration: 32-row, 16-row and 1280-pixel wide-row tests preserve burst/pixel counts with `error=0`. Combined RTL syntax compile for reader, writer, `axi_rgb565_dma8`, FIFO, scheduler and parser passes. The actual Efinix bridge/DDR flow and board tests remain necessary to validate hardware outstanding behavior and timing.

## GPU throughput bottleneck plan after AXI user-clock test (2026-10-29)

### Correct interpretation of the clock tests

- There are two separate observations: raising the GPU main/compute clock to 250 MHz previously improved throughput; with that GPU clock already at 250 MHz, raising only AXI/userclk from 200 MHz to 250 MHz did not improve throughput. The first result proves that GPU-side clocked work was a real bottleneck before the earlier change. The second means AXI user-clock frequency is no longer the active limit by itself; it does not mean the AXI/DDR transaction path is unconstrained.
- A likely explanation is that the compute-clock increase moved the bottleneck to DDR fixed-time latency, controller/bridge scheduling, shared read ownership, parser read-before-write dependencies, or command retirement. DDR CAS and controller queue time are measured in nanoseconds; increasing AXI clock does not shorten them. If service latency remains constant in nanoseconds, its cycle count rises at 250 MHz, so serialized read starts can have nearly unchanged throughput.
- Clock-domain caveat: in the checked `rtl/ddr3_soc_top.v`, `user_clk` is assigned directly from input `core_clk` in both `ASYN_AXI_CLK` branches, and `GPU_top` clocks parser, scheduler, reader, writer, and command slave from `user_clk`. `ddr3_top` receives this as `axi_clk` and `feedbk_clk` as its separate `core_clk` with `.ASYN_AXI_CLK(1)`. The source does not expose an independent GPU logic clock and AXI user clock inside `GPU_top`; the two measured revisions must therefore be mapped to the actual programmed top/bitstream before assigning exact clock-domain names.
- Current GPU defaults are `CORE_COUNT=4`, `READ_BURST_LEN=16`, and `WRITE_BURST_LEN=16` in `rtl/GPU/GPU_top.v`. The DMA has separate read and write owners, but source and destination readers share one read owner. Verify the generated implementation hierarchy if a wrapper overrides these parameters.
- The internal DDR AXI interface is 128-bit while the visible GPU AXI port is 32-bit and passes through the SoC conversion layer. Raw DDR bandwidth therefore cannot be compared directly with GPU 32-bit handshake counts. The lack of gain from raising only AXI userclk suggests this clock is not limiting accepted data beats; it says nothing by itself about DDR service latency or serialization behind the converter.
- The current source has the reader's one-entry AR prefetch and retains its read owner across ordinary burst boundaries; this current-source check supersedes older 2026-10-28 notes describing release/re-arbitration on every burst. The PnR SDC currently constrains input `core_clk` at 4 ns, while `user_clk` is a direct RTL alias of that input.

### Period accounting for one 32x32 instruction

- Colorfill: 32 rows x 64 bytes, approximately 512 W data beats, 32 AW handshakes, and 32 B responses. There is no reader traffic.
- BLT: approximately 512 source R beats plus the same 512 W beats. Sprite/alpha adds a second destination read, approximately 1024 R beats plus 512 W beats.
- Re-ran `sim/reader_writer_compare/run.do` against the current worktree on 2026-10-29. The 64-byte reader row took 20 cycles with continuous R data; the 32-pixel writer row took 54 cycles including 32 pixel/FILL handshakes, AW, 16 W beats, and B. These exclude DDR latency, arbitration, HDMI traffic, and parser waits. This run supersedes the older 19/85 local figures above for analysis of the current writer.
- The current local lower bounds are about `32*54 = 1728` cycles for a 32x32 colorfill, `32*(20+54) = 2368` for BLT, and `32*(20+20+54) = 3008` for a two-read sprite. The much larger board ratios (BLT about 3.9x colorfill and sprite about 6.8x) correspond arithmetically to roughly 137 extra cycles per read row if all work were serialized: `54+20+137` is about 3.9x the writer row, and `54+2*(20+137)` is about 6.8x. This is only a normalized equivalent, not evidence that each row actually waits 137 cycles; the parser batches reads, so counters are needed to identify whether the excess is controller latency, arbitration, or parser scheduling.
- This is latency and scheduling amplification, rather than exhaustion of nominal memory bandwidth. Increasing the clock cannot remove a per-row wait imposed by the controller or by a read-before-write dependency.
- For scale, if the stress result is 2300 accepted 32x32 fills per 60 Hz frame, the pixel payload is `2300*32*32*2*60 = 282.6 MB/s`. A 32-bit AXI W channel at 200 MHz carries 800 MB/s before protocol gaps, so this workload's payload alone is about 35% of that single-channel ceiling. This leaves theoretical headroom, but accepted commands are not the same as completed writes; use AW/W/B counters and completed-command counts before concluding the bus has that spare capacity.

### Current likely bottlenecks

1. `axi_rgb565_reader.v` streams R beats once they arrive and can pre-issue one following burst while R is active, retaining ownership across the command. It still has a first-burst startup and shares one `read_owner` across all cores' source and destination readers. Source and destination readers therefore cannot progress concurrently; competing cores can also wait for the current reader command to finish.
2. `bitblt_cmd_parser.v` waits for the complete read batch (`read_seen`, and for sprite/alpha also `dst_read_seen`) before entering `ST_BATCH_WRITE`. This prevents a completed row from being written while a later row is fetched and exposes the whole read latency before a write can progress.
3. The parser also waits for `write_seen` before advancing its row/batch state. Available FIFO data is controlled by whole-batch `read_done`/`write_done` pulses instead of FIFO watermarks.
4. Colorfill has no reader, so its remaining limits must be in command submission/scheduling, writer staging/serialization, or write-side DDR response/service. The writer's local staging lets FILL happen before owner grant, but each core has only one staging burst and the DMA has one global write owner. For BLT/sprite, reader and parser costs are added to this shared write path.
5. The scheduler reads seven 32-bit words for every command and dispatches only to an idle parser. `HDMI_Stress` additionally polls `GPU_STATUS_PROG_FULL` and delays after accepted fills; its ISR prints once per frame. These software costs can cap accepted commands, but not the BLT/colorfill ratio when accepted counts are equal.
6. HDMI is a second DDR master. At 1280x720 RGB565/60 Hz it needs about 27.648 million 32-bit R beats/s, or 13.8% of a 200 MHz cycle budget before controller overhead. It can increase arbitration latency, but is not sufficient by itself to explain 3.9x/6.8x.

### Measurements to add before changing datapaths

Add saturating counters at the GPU AXI master boundary, readable through a small status register block or printed at frame boundaries:

- AR/AW and R/W/B handshakes;
- AR handshake to first accepted R beat;
- maximum and accumulated gap between accepted R beats;
- cycles of `RVALID && !RREADY`;
- owner release to next AR handshake;
- read/write owner occupancy, grant wait, and source/destination owner switches;
- parser cycles in read wait, `ST_BATCH_WRITE`, and write wait;
- command FIFO `PROG_FULL` cycles and accepted/rejected software submissions.

These counters distinguish DDR first-data latency, inter-beat stalls, owner arbitration, and parser serialization. For the AXI userclk comparison, record both cycles and elapsed time: if AR-to-first-R cycles increase while nanoseconds stay constant, the wait is downstream of the clock-rate change. Also confirm effective clocks at the actual GPU and DDR AXI pins for each bitstream.

### Ordered optimization plan

1. Extend `sim/reader_writer_compare` with four parser/reader instances and an AXI responder that independently controls AR-to-first-R latency, R beat gaps, B latency, and HDMI background reads. Run colorfill, BLT, and sprite streams with equal accepted-command counts.
2. Remove parser batch serialization: prepare/issue the next source row while the writer drains the previous row, using FIFO occupancy and row tags so an unconsumed row is never overwritten.
3. Give each reader a small descriptor queue (at least two pending row/burst descriptors). Keep the current same AXI ID unless the SoC bridge contract permits response reordering, and retain ownership while queued descriptors are outstanding.
4. For sprite/alpha, either provide separate read-owner queues with response routing or accept source and destination ARs into a shared ordered queue. Verify same-ID/outstanding behavior against the SoC bridge before enabling it.
5. Reduce owner churn with a bounded burst quantum, then rotate among pending readers and the writer so HDMI is not starved.
6. Only after reader/parser overlap is measured, evaluate a second writer burst buffer and command batching. These can improve colorfill and CPU submission overhead but do not fix the current read-dominated BLT/sprite gap.

### Acceptance criteria

- Preserve display/pending/render frame ownership and never change an HDMI address during an active line.
- Preserve FIFO keep/last, 4-KB burst splitting, stride, and OP=4 behavior.
- Require ModelSim scoreboards for colorfill, BLT, sprite, odd widths, 4-KB boundaries, stalled RREADY/WREADY, and multiple cores before board testing.
- Compare accepted commands/frame, completed commands/frame, AXI counters, and image checksum. A higher handshake count without matching completed commands or image correctness is a regression.

## AXI/parser performance instrumentation (2026-10-29)

The saved test version now contains non-invasive counters for the first measurement pass. The counters are in `rtl/GPU/GPU_top.v` and are exposed by `rtl/GPU/axi_gpu_cmd_axi_slave.v`:

| Offset | Meaning |
|---:|---|
| `0x08` write bit 0 | Clear all performance counters |
| `0x0c` | GPU AR handshakes |
| `0x10` | GPU R handshakes |
| `0x14` | GPU AW handshakes |
| `0x18` | GPU W handshakes |
| `0x1c` | GPU B handshakes |
| `0x20` | Sum of sampled AR-to-first-R latency |
| `0x24` | Maximum sampled AR-to-first-R latency |
| `0x28` | `RVALID && !RREADY` cycles |
| `0x44` | Maximum gap between accepted R beats |
| `0x2c` | Read-owner active cycles |
| `0x30` | Write-owner active cycles |
| `0x34` | Read request waiting while no read owner is active |
| `0x38` | Write request waiting while no write owner is active |
| `0x3c` | Parser command-dispatch pulses |
| `0x40` | Parser done pulses |
| `0x48` | Parser cycles in read-issue/wait states |
| `0x4c` | Parser cycles in write-wait state |

The AR-to-first-R counter is intentionally a low-cost sample: it tracks one pending AR at a time. It is suitable for the current ordered/same-ID reader path, but it must be replaced by an ID-indexed outstanding table if multiple independent response streams are enabled later. The counters are not in the reader/writer data path and do not change their valid/ready handshakes.

`HDMI_Alpha/src/2D_DMA.c` now provides `gpu_perf_clear()`, `gpu_perf_read()`, and a test-only `bitblt_async()` that does not reject commands merely because the GPU status is busy. The normal `bitblt()` behavior is unchanged. `HDMI_Alpha/src/HDMI_show.c` is currently a dedicated benchmark:

- Runs 64 fixed 32x32 commands for colorfill, BLT, and sprite.
- Submits eight complete commands into the command FIFO before waiting, so the four parser instances can be exercised concurrently.
- Uses framebuffer 2 as the destination and does not invoke frame switching.
- Clears counters before each operation class and prints command, completion, AXI, first-R latency, owner, and wait counts after each class.

Build verification on 2026-10-29:

- ModelSim `vlog` compilation passed for `axi_gpu_cmd_axi_slave`, reader, writer, DMA, parser, scheduler, FIFO, and `GPU_top` after adding the parser state counters and R-gap counter.
- `HDMI_Alpha` software build passed using `make.exe` with `SHELL=D:/26FPGA/RISCV-IDE/build_tools/bin/sh.exe`, `RISCV_BIN=D:/26FPGA/RISCV-IDE/toolchain/bin/riscv-none-elf-`, and `BSP=efinix/EfxSapphireSoc`. Result: 700208 B of 1984 KB RAM used.
- `sim/tb_axi_gpu_perf_regs.v` passed in ModelSim: a counter read at `0x0c` returns the supplied value, and a clear write at `0x08` asserts `perf_clear` without asserting `cmd_fifo_wr_en`. This protects the command FIFO from debug-register writes.
- The next hardware run should capture the three printed counter groups. Interpret `cmd`/`done` first; then compare `AR`, `firstR_sum/max`, owner wait, and `AW/W/B`. A counter mismatch or `done < submitted` is a correctness issue before it is a performance result.

## Measurement pass 2: active gaps, contention, and continuous submission (2026-10-30)

The measurement RTL now adds the following read/write counters:

| Offset | Meaning |
|---:|---|
| `0x54` | Cycles with read owner active and `RVALID=0` |
| `0x58` | Cycles with read owner active while another read request is pending |
| `0x5c` | Cycles with write owner active while another writer requests the owner |
| `0x60` | `AWVALID && !AWREADY` cycles |
| `0x64` | `WVALID && !WREADY` cycles |
| `0x68` | Sum of AW-to-first-W intervals |
| `0x6c` | Maximum AW-to-first-W interval |
| `0x70` | Maximum gap between accepted W beats within an active write burst |
| `0x74` | Sum of final W-to-B intervals |
| `0x78` | Cycles after final accepted W beat while B is not valid |

The completion counter now adds the number of set bits in `parser_done`, so simultaneous core completions are counted separately. `HDMI_Alpha/src/HDMI_show.c` runs each operation in two modes: the original eight-command batches with a wait after every batch, then a continuous 64-command enqueue with one final wait. The test keeps the same command size, addresses, and operation ordering. The continuous mode still obeys command FIFO backpressure through `bitblt_async()`.

Active read contention excludes the current owner and only counts a pending command/bus request from another source/destination reader. Active write contention excludes the current write owner. W-gap counting resets at AW and ends at WLAST; B-wait starts after WLAST, not at AW.

Build verification for this pass:

- GPU RTL ModelSim compilation passed after adding the new DMA, top-level counter, parser, and register connections.
- `HDMI_Alpha` rebuilt successfully; RAM use is 700864 B of 1984 KB.
- The debug-register ModelSim test still passes. Its old positional omission warnings are benign for the standalone testbench because the new performance inputs are unconnected and the tested register behavior is unchanged; use named connections when extending that testbench further.

The first BATCH/CONT run is recorded below. This second instrumentation pass now additionally records active-burst R gaps, read-owner-release to next AR delay, and AR-to-first-R sample/orphan counts. Software now reports CPU cycles spent inside command submission and `bitblt_wait()` separately. The new active gap excludes idle time between R bursts; the release-to-AR interval is a DMA owner/arbitration metric and should be interpreted together with its sample count.

Instrumentation added after reviewing this run:

| Offset | Meaning |
|---:|---|
| `0x7c` | Sum of cycles between accepted R beats while an R burst is active |
| `0x80` | Maximum cycles between accepted R beats within an active burst |
| `0x84` | Sum of read-owner release to next accepted AR intervals |
| `0x88` | Maximum read-owner release to next accepted AR interval |
| `0x8c` | Number of measured owner-release to AR intervals |
| `0x90` | Number of matched AR-to-first-R samples |
| `0x94` | Orphan/overflow count for AR-to-first-R matching |

`AR` timestamps are held in a two-entry ordered queue, matching the reader's active transaction plus one prefetched AR. The test reads the new high register offsets; this caught and fixed the AXI slave address decoder, which previously compared only `araddr[6:0]` and aliased offsets above `0x7c`. The register test now checks `0x94` explicitly.

Verification after adding these counters:

- ModelSim compiled `axi_gpu_cmd_axi_slave.v` and `GPU_top.v` successfully.
- `sim/tb_axi_gpu_perf_regs.v` passed, including the `0x94` high-offset read and isolation of performance clear from command FIFO writes.
- `HDMI_Alpha` software rebuilt successfully; RAM usage was 701,232 B of 1,984 KB. Its output now includes CPU `submit_cycles` and `wait_cycles`, active R-gap statistics, owner-release-to-AR statistics, and first-R sample/orphan counts.
- These updated counters require a rebuilt FPGA bitstream and a matching rebuilt software image. The supplied 16:23 output predates this instrumentation and cannot provide the new metrics.

## No-result run follow-up (2026-10-30)

The 16:58 run printed only the benchmark title and fixed command dimensions, then stopped before any result line. This does not establish that the GPU itself timed out: before stage markers, the first silent operations were `gpu_perf_clear()` followed by command submission, and a blocking AXI MMIO transaction would not reach the driver's FIFO polling timeout or `bitblt_wait()` timeout.

The 17:06 follow-up confirms `case start` and both `perf clear begin` / `perf clear returned`, so the clear write completes. It stops before the first command returns. The next diagnostic build prints before the first `bitblt_async`, around the first two FIFO-status reads, and around their command-register writes. These outputs separate an AXI status-read hang, FIFO FULL/PROG_FULL polling, and a command-register write hang.

The 17:11 log stopped immediately after `perf clear returned`, before `first command begin`. The first-command marker was preceded by `csr_read(mcycle)` used for CPU timing. This SoC build's exception path does not safely support that CSR access in this test, so the software entered an exception before reaching the GPU command sender. Removed the `mcycle` timing and `riscv.h` dependency from the benchmark; the latest build succeeds at 701,744 B / 1,984 KB RAM. GPU hardware counters remain available, while CPU submit/wait timing is deferred until a known-good cycle-counter interface is identified.

## Completed diagnostic measurement run (2026-10-30 17:15)

All six cases completed after removing `mcycle`. The run used the diagnostic firmware with UART messages around counter clear, the first command, and the first batch wait. This proves the test and command path run end-to-end, but its elapsed-cycle values include UART diagnostic time and must not be used as clean throughput numbers.

| Operation | Mode | Elapsed cycles (diagnostic; do not compare) | AR/R | AW/W/B | Read owner | Active no-R | Active R-gap sum/max | First-R sum/max/samples | Release-to-AR sum/max/count |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Colorfill | BATCH | 6,724,319 | 0 / 0 | 2,072 / 32,768 / 2,072 | 0 | 0 | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| BLT | BATCH | 3,126,631 | 2,080 / 32,768 | 2,072 / 32,768 / 2,072 | 149,198 | 116,430 | 5,776 / 109 | 255,351 / 315 / 2,080 | 1,959,255 / 1,086,811 / 63 |
| Sprite | BATCH | 3,233,531 | 4,152 / 65,536 | 2,072 / 32,768 / 2,072 | 276,106 | 210,570 | 7,790 / 107 | 470,392 / 265 / 4,152 | 1,941,200 / 1,072,782 / 127 |
| Colorfill | CONT | 1,985,036 | 0 / 0 | 2,072 / 32,768 / 2,072 | 0 | 0 | 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| BLT | CONT | 2,113,154 | 2,080 / 32,768 | 2,072 / 32,768 / 2,072 | 137,602 | 104,834 | 5,382 / 108 | 232,730 / 329 / 2,080 | 957,546 / 821,614 / 63 |
| Sprite | CONT | 2,230,791 | 4,152 / 65,536 | 2,072 / 32,768 / 2,072 | 259,111 | 193,575 | 6,353 / 108 | 439,447 / 277 / 4,152 | 955,522 / 819,783 / 127 |

All AR-to-first-R orphan counts were zero. `Rstall`, AW stall, and W stall were zero in every case. Read payloads and write payloads exactly match 64 commands of 32x32 RGB565: BLT reads 32,768 32-bit beats, sprite reads 65,536, and each class writes 32,768. This is sufficient to establish that the workload completed its expected memory traffic and that GPU FIFO backpressure did not stall R delivery.

The AR-to-first-R averages are about 123 cycles for batched BLT, 112 for continuous BLT, 113 for batched sprite, and 106 for continuous sprite. These measure from accepted AR at the GPU's AXI master interface to first accepted R, so they include the SoC interconnect/upsizer/DDR-controller path. They show large exposed read startup latency, but cannot assign the delay to a specific SoC sub-block without a counter at the controller side.

Active-burst R-gap totals are small compared with the R beat counts (roughly 0.12-0.18 gap cycles per beat interval), while the maxima around 107-109 cycles reveal rare long gaps. With zero RREADY stalls, those gaps originate upstream of the GPU reader FIFO. This points to response service interruption/latency variation rather than local FIFO backpressure.

Do not interpret release-to-AR sums/maxima as pure arbitration delay. The counter measures every owner release until a later AR, even when no other parser has an eligible request; batch boundaries, command issue gaps, and diagnostic UART time can inflate it. Its large maxima (0.8-1.1 million cycles) confirm that this metric is dominated by periods without an immediately pending competing read request in this test. A future arbitration-specific counter must start only when owner is NONE and a different reader request is already pending, then stop at the next AR handshake.

`done` was 64 for COLOR and 63 for BLT/SPRITE, while all `bitblt_wait()` calls returned and AXI payload totals match all 64 commands. Preserve this as a counter/completion-pulse discrepancy to check against the exact programmed bitstream; it does not invalidate the observed AXI payload counts, but completion-count accuracy is not fully signed off.

### Optimization readiness

The data is sufficient to begin the user's planned first optimization pass after manually restoring the stable RTL baseline. Start with the reader's exposed AR-to-first-R latency and rare upstream R gaps; keep the writer/parser baseline fixed during that experiment. Do not use the diagnostic `cycles` or release-to-AR values as a performance baseline. After restoring the clean software benchmark (or removing temporary UART markers), capture a no-debug run for end-to-end elapsed cycles, and compare it with these reliable transaction counts and latency counters. The current read path supports at most one prefetched AR beyond the active burst, so increasing read queue depth or batching more rows per request are grounded candidate experiments, subject to the SoC AXI bridge's outstanding-transaction limits.

Added temporary diagnostic UART markers to the `HDMI_Alpha` test around performance-counter clear, first command return, and first batch wait, then extended them around first-command FIFO reads/writes after the 17:06 result. Latest diagnostic software build succeeded (701,904 B / 1,984 KB RAM). This diagnostic image is for locating the software-visible stall only; its extra UART output invalidates cycle measurements. Interpret the next run as follows:

- No `case start`: the newly built diagnostic image was not loaded.
- `perf clear begin` without `perf clear returned`: the CPU is stuck completing the GPU counter-clear MMIO write, pointing to the SoC AXI peripheral path or address map.
- `perf clear returned` without `first command returned`: the command FIFO status read or command MMIO writes are not returning; inspect GPU AXI slave mapping/handshake and FIFO status.
- `first command begin` without `gpu word 0 status read begin`: the call path itself did not enter the command sender.
- `gpu word 0 status read begin` without `gpu word 0 status=...`: the FIFO status AXI read is stalled or does not return.
- A printed `fifo full/progfull` with no later status line means the status read returns, but FULL/PROG_FULL stays asserted; the timeout should print if polling continues.
- `gpu word 0 ... write begin` without `gpu word 0 write returned`: the command-register AXI write is stalled.
- `first command returned` and `first batch wait begin` without wait return/timeout: command dispatch or GPU completion/status logic is stalled. The `bitblt_wait()` software timeout should print if its status reads continue to return.
- A printed `batch wait failed` or `GPU wait timeout` is a returned status failure rather than a CPU bus hang; preserve the status/FIFO values in the log.

## Second hardware performance-counter results (2026-10-30)

User ran the same fixed 32x32, 64-command COLOR/BLT/SPRITE workload with eight-command batches and waits, then with continuous submission and one final wait.

| Operation | Mode | Elapsed cycles | Cycles/command | AR/R | AW/W/B | Read owner | Write owner | Active no-R | Read contention | Write contention |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Colorfill | BATCH | 98,750 | 1,542.97 | 0 / 0 | 2,072 / 32,768 / 2,072 | 0 | 76,280 | 0 | 0 | 36,425 |
| BLT | BATCH | 188,109 | 2,939.20 | 2,064 / 32,768 | 2,072 / 32,768 / 2,072 | 131,008 | 76,280 | 98,240 | 63,160 | 894 |
| Sprite | BATCH | 310,412 | 4,850.19 | 4,136 / 65,536 | 2,072 / 32,768 / 2,072 | 260,376 | 76,280 | 194,840 | 201,646 | 58 |
| Colorfill | CONT | 145,648 | 2,275.75 | 0 / 0 | 2,072 / 32,768 / 2,072 | 0 | 76,280 | 0 | 0 | 4,423 |
| BLT | CONT | 262,380 | 4,099.69 | 2,064 / 32,768 | 2,072 / 32,768 / 2,072 | 123,064 | 76,280 | 90,296 | 7,941 | 61 |
| Sprite | CONT | 382,653 | 5,978.95 | 4,136 / 65,536 | 2,072 / 32,768 / 2,072 | 245,456 | 76,280 | 179,920 | 131,145 | 0 |

### Interpretation

- Bus payload counts match across submission modes. All classes write 32,768 32-bit beats; BLT reads 32,768 and sprite reads 65,536. The 2,072 AW/B transactions cover the 2,048 expected 64-byte rows plus splits at 4-KB boundaries.
- The read path remains the clearest GPU-side bottleneck for BLT and sprite. `Rstall=0` means the GPU never deasserted `RREADY` while data was valid. Yet the read owner was active without `RVALID` for 75% of BLT read-owner cycles (98,240/131,008), and 75% of sprite read-owner cycles (194,840/260,376). This points to response latency/turnaround and owner retention between bursts, rather than a full destination FIFO blocking returned data.
- Current owner behavior is relevant: the reader holds the owner across its queued next AR, but yields when a burst boundary occurs without a prefetched descriptor. With 32x32 commands and 64-byte rows, each row is one 16-beat burst; any gap before the next row can therefore become visible inside the same multi-row command. The new active-burst R-gap and release-to-AR metrics are needed to distinguish DDR/bridge latency from owner re-arbitration. The current `active_no_R` also includes the pre-first-R delay and between-burst intervals.
- Writes show no `AW` or `W` backpressure: `AWstall=0`, `Wstall=0`, and accepted AW-to-first-W is one cycle per burst (`sum=2,072`, `max=1`). `WtoB=37,296` sums final-W-to-B intervals (18.0 cycles/burst); `Bwait=35,224` counts cycles after WLAST while BVALID is low (17.0 cycles/burst). Write-owner occupancy is 76,280 cycles (36.8 cycles/burst). The B response wait therefore accounts for a substantial part of the write-owner time, while accepted W payload accounts for 16 beats/burst. The remaining roughly 3.8 cycles/burst covers AW/first-W timing, response handshake accounting, and owner/DMA control. `WtoB` includes the final beat-to-response handshake interval, while `Bwait` stops counting once BVALID is asserted.
- BATCH is substantially faster than CONT in elapsed cycles: 98,750 vs 145,648 for colorfill, 188,109 vs 262,380 for BLT, and 310,412 vs 382,653 for sprite. This is opposite the simple expectation that continuously filling the command FIFO should reduce gaps. The AXI traffic totals are unchanged, while read-owner cycles are somewhat lower in CONT and contention falls sharply. Therefore the extra elapsed time is not extra memory payload. It likely comes from the CPU command submission/backpressure path or the measurement window including longer CPU FIFO stalls; the newly added CPU `submit_cycles` and `wait_cycles` should settle this on the next bitstream. Do not label CONT as worse GPU throughput until separating CPU enqueue time from hardware drain time.
- Aggregate parser wait counters sum all cores' state cycles and may exceed elapsed time. Owner and active-no-R counters are also wall-clock condition counts and should be divided by the corresponding class elapsed cycles only when the measurement interval is valid.

### Counter validation and next measurement

- `cmd=64` but `done=63` persists even though current RTL adds the population count of `parser_done`. The hardware run is only trustworthy if the programmed bitstream includes this RTL. Since `bitblt_wait()` returned successfully, 63 may reflect the counter clear/first completion window or a mismatch between command dispatch and parser completion pulse; verify the bitstream revision and inspect the counter on a short 1-command test before treating `done` as a lost command.
- Previous `firstR_sum/max` values (for example 94,614 cycles for BLT BATCH) are invalid as AR-to-first-R latency because the scalar timestamp was overwritten/mispaired by reader prefetch. The updated measurement uses a two-entry ordered timestamp queue, matching the reader's maximum one prefetched AR in addition to the active burst. Use the added `firstR_samples` and `orphan` counters to validate that the queue drained without unmatched first R beats. Same-cycle AR and first-R handshakes are counted as zero-cycle samples.
- `Rgap_max` in the previous run included idle gaps between bursts and CPU stalls. The new `Rgap_active_max` only measures gaps between accepted R beats inside a burst; `Rgap_active` sums cycles with an active burst and no accepted R beat. `release_to_AR_sum/max/count` tracks owner release to the next accepted AR; zero samples means no reader owner release occurred during that workload.
- The BATCH/CONT comparison must use software `submit_cycles` and `wait_cycles` from the new software image. It should also be repeated with `done == submitted`; otherwise stale command completions can affect the boundaries.
- If the new active R-gap sum is small but `firstR` average is large, investigate AR-to-response latency in the SoC upsizer/DDR controller. If release-to-AR is large, the read-owner arbitration/regrant path is exposed. If both are small while parser read waits remain large, inspect parser/FIFO sequencing. Only then proceed with reader-side prefetch or independent read/write scheduling changes.

## First hardware performance-counter results (2026-10-30)

User ran the dedicated HDMI_Alpha test at 32x32, 64 commands/class, with batches of 8 commands and `bitblt_wait()` after every batch. Reported results:

| Operation | Elapsed cycles | Cycles/command | Relative cycles | R beats | W beats | AR/AW/B | Read owner cycles | Write owner cycles |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Colorfill | 98,592 | 1,540.5 | 1.00 | 0 | 32,768 | 0 / 2,072 / 2,072 | 0 | 76,280 |
| BLT | 190,427 | 2,975.4 | 1.93 | 32,768 | 32,768 | 2,080 / 2,072 / 2,072 | 134,010 | 76,280 |
| Sprite | 314,496 | 4,914.0 | 3.19 | 65,536 | 32,768 | 4,152 / 2,072 / 2,072 | 261,960 | 76,280 |

### What the counters support

- Payload handshakes match 64 commands of 32x32 RGB565: each class writes `64*32*32/2 = 32,768` 32-bit W beats; BLT reads 32,768 R beats; sprite reads 65,536 R beats. `W` is identical across classes, while R payload doubles from BLT to sprite.
- Elapsed cycles scale close to the number of 32-bit payload streams: `1 : 1.93 : 3.19` for colorfill, BLT, sprite, compared with expected data-volume ratios `1 : 2 : 3`. This strongly indicates that the measured cost follows memory traffic, rather than alpha/color arithmetic or command dispatch alone. The small deviation can come from operation setup, burst splits, and software batching.
- If `user_clk` was 250 MHz during this run, payload rate is approximately 332 MB/s for colorfill W, 344 MB/s combined R+W payload for BLT, and 313 MB/s combined R+W payload for sprite. These are logical AXI payload bytes per elapsed cycle, not a direct DDR PHY utilization measurement. They remain below the nominal 400 MB/s half-bandwidth target discussed by the user, so the counters do not support a claim that the design already reaches that target.
- Read-owner occupancy divided by accepted R beats is about 4.09 cycles/beat for BLT and 4.00 cycles/beat for sprite. `RVALID && !RREADY` is zero in all classes, so GPU FIFO backpressure is not causing the observed read-owner occupancy. The owner is active substantially longer than the R data handshakes themselves; the remaining time is in AR/first-data wait, gaps where no R beat is presented, and/or bridge/controller scheduling. This is the strongest direct evidence that read service efficiency is the main next investigation.
- Write-owner occupancy is 76,280 cycles for all three classes, or about 2.33 owner cycles per W beat and 36.8 owner cycles per AW/B burst. Because the writer fills its local burst before requesting ownership, this interval covers grant through AXI write completion. AW/B counts are 2,072 rather than the nominal 2,048 because some 64-byte rows cross 4-KB boundaries and are split; the W payload count remains unchanged. Add AW/W/B stall/latency counters before deciding whether the extra owner time is mainly AWREADY, WREADY, or B response wait.
- Aggregate parser counters sum state-cycles over all four cores, so they can exceed elapsed wall cycles. Colorfill's 199,511 write-wait cycles, for example, are not 199,511 elapsed cycles; they represent overlapping parser-core wait time. Read-state counter similarly includes both ST_ISSUE and ST_SPRITE cycles and is not a pure external DDR wait counter.

### Counter and test-window caveats

- `done=63` does not show that one command failed. The counter currently increments once when `|parser_done` is true, so two or more parsers completing in the same clock count as one completion event. The test's eight `bitblt_wait()` batches all returned successfully and the command dispatch count is 64. Change this counter to add the population count of `parser_done` before using it as a completed-command count.
- `firstR_sum/max` is not reliable for this prefetching reader. It tracks only one pending timestamp, while a second AR can handshake before the prior response's first R beat; simultaneous AR/R updates can also overwrite the pending timestamp. Replace it with an ordered timestamp FIFO sized to the maximum outstanding AR count, or measure per-reader request/first-beat pairs.
- `Rgap_max=2617` spans the entire test class, including software's `bitblt_wait()` after each 8-command batch and intervals with no read request outstanding. It is not necessarily a maximum gap inside an active read-owner interval. Add an active-owner-only R gap statistic, plus a count of RVALID-low cycles while a read owner is active.
- `read_wait`/`write_wait` only count requests pending while the corresponding owner is NONE. They miss a request queued behind an already active owner, so their small values do not establish absence of inter-core arbitration contention. Add explicit `request_pending && owner_active && requester_is_not_owner` counters.
- Since the test waits after each batch, elapsed cycles include software batch/drain gaps and do not represent a continuously saturated command stream. The current values are valid end-to-end results for this exact test protocol. A separate continuous-submission run should enqueue all 64 while allowing the command FIFO checks to stall the CPU as needed, then wait once, to measure maximum sustained multi-core work.

### Updated next steps

1. Correct `done_count` to count completed parser bits, then add active-owner-only R gap and read/write contention counters; replace scalar AR-first-R timing with an ordered timestamp queue.
2. Add write-side timing: AWVALID&&!AWREADY cycles, WVALID&&!WREADY cycles, AW handshake to first W, max accepted-W gap, and final W to B handshake. This will separate DDR/bridge response delay from local staging and owner control.
3. Run two software windows with the same 64 commands: existing 8-command batches with waits (current reproducible result) and continuous enqueue with a single final wait. Keep operation/addresses/bus clock/HDMI scan identical.
4. If active-owner R gaps dominate and RVALID&&!RREADY remains zero, prioritize reader request/response scheduling and parser read/write overlap. If write B or W stalls dominate colorfill, prioritize write-side burst service. Do not change the datapath until these counters distinguish those cases.


## Reader 顺序1：降低首个 AR 启动延迟（2026-10-30）

在 `rtl/GPU/axi_rgb565_reader.v` 中，将首个 burst 的地址、beat 数、有效字节数、半字节偏移、行结束标志和命令结束标志，直接在 `ST_IDLE` 接收命令的同一拍计算并锁存到 candidate 描述符。这样命令接收后不再额外等待一个 candidate-build 周期即可进入 `ST_AR`。

这次只改 reader 的首个描述符启动路径，未改 writer、parser、DMA owner/仲裁、FIFO 格式或帧切换逻辑。后续 burst 仍沿用原有 descriptor queue、4KB 边界拆分、stride、keep/last 和多行推进逻辑。

ModelSim 回归目录：`sim/reader_writer_compare`。

- `READER_DESC_DEPTH=4`：通过。
- `READER_DESC_DEPTH=2`：通过。
- 64 字节单行、首响应延迟 0：19 cycles。
- 64 字节 × 32 行：546 cycles，512 beats，32 个 AR。
- 32 字节 × 16 行、首响应延迟 32：346 cycles，128 beats，16 个 AR。
- 2560 字节单行：682 cycles，640 beats，40 个 AR。

与本次修改前的同一仿真基线相比，上述场景均减少 1 个 reader 时钟周期。`first_AR_to_firstR` 仍由仿真内存模型的响应延迟决定，没有变化；因此本优化降低的是 reader 命令到首个 AR 的软件/逻辑启动开销，不会改变 DDR/AXI 控制器的物理 AR 到首个 R 延迟。

## Reader 连续行 AR 预取（2026-10-30）

上一轮“首个 AR 在 ST_IDLE 直接计算”的改动已撤回。该改动把地址、边界和长度计算放到了命令接收路径，可能拉低 core_clk；当前恢复为命令接收后装载 issue 游标，下一拍在 `ST_AR/ST_R` 生成 candidate。

本轮仅针对 reader 做连续行预取：

- `axi_rgb565_reader.v` 的 descriptor queue 默认深度由 4 扩为 32；DMA 中 source reader 和 destination reader 都显式使用 `DESC_DEPTH(32)`。
- 每次 AR 握手后，直接根据已接受 descriptor 的行边界、4KB 边界和剩余行数计算并锁存下一 descriptor。只要还有数据，下一拍即可继续 AR，避免等待当前 R burst 完成后才重新生成地址。
- `24x24`（每行 48 字节）只产生 24 个 AR；`32x32`（每行 64 字节）产生 32 个 AR；不会为了填满 32 项而发出超出命令范围的 AR。4KB 拆分和 stride 推进仍由原有计算保持。
- 该实现仍保持 AXI 同 ID、按 AR 顺序消费 R descriptor；没有改 parser、FIFO 数据格式、DMA owner 或 writer 协议。candidate 的下一项是寄存器锁存，避免在 R 数据路径中加入长组合级联。

ModelSim 回归：`sim/reader_writer_compare/run.do` 默认 descriptor 深度改为 32，响应模型队列同步改为 32。以下场景均通过，无 error、done=1，且 AR/beat/pixel 数符合预期：

| 场景 | AR | R beats | 像素 | 仿真 cycles |
|---|---:|---:|---:|---:|
| 64 bytes x 32 rows | 32 | 512 | 1024 | 547 |
| 32 bytes x 16 rows | 16 | 128 | 256 | 347 |
| 48 bytes x 24 rows | 24 | 288 | 576 | 315 |
| 2560 bytes x 1 row | 40 | 640 | 1280 | 683 |

仿真中 64-byte x 32-row 和 48-byte x 24-row 在响应无额外延迟时，首 AR 到首 R 为 2 个模型周期，连续 R beat 间隔最大为 1 个周期，`RVALID&&!RREADY` 为 0。尚未综合布局布线，实际 core_clk 还需要用当前工程报告确认；若时序下降，应优先回退“每拍计算下一 descriptor”的实现，再保留队列深度扩展单独评估。
## Reader 时序修复（2026-10-30）

最新布局布线报告显示最差 `core_clk -> core_clk` 路径为
`u_gpu_top/dma8/DMA_CORE[3].dst_reader/issue_rows_left -> candidate_bytes`，延迟约
`5.15 ns`，其中包含 AR 握手同拍计算的 `next_issue_*`、4KB 边界
`calc_burst_beats/calc_burst_bytes` 以及行结束判断，导致 250 MHz（4 ns）违例约
`1.28 ns`。

本次修复仅撤掉 AR 握手同拍的下一 descriptor 大组合计算：AR 握手时更新 issue
游标并清除 `candidate_valid`，下一时钟由既有 candidate-build 逻辑根据已更新游标
生成下一 descriptor。保留 `DESC_DEPTH=32` 和已完成的 outstanding AR 描述符队列，
因此没有改变 AXI 数据协议、行地址、4KB 边界或 FIFO 数据顺序；代价是相邻 AR
之间至少留出一个候选生成周期，优先恢复 core_clk 时序。

ModelSim `sim/reader_writer_compare` 回归通过：64x32、32x16、48x24、2560x1
以及初始 R 延迟场景均 `done=1/error=0`，`RVALID&&!RREADY=0`，像素数和 AR
数量正确。需要重新综合布局布线确认实际 core_clk；不应直接用旧报告判断修复后频率。

## Sprite 闪屏修复（2026-10-30）

检查发现 `gpu_sync_fifo` 的 32 位写入、16 位读取路径在 `wr_keep=2'b01`
（一个 AXI beat 只有低 16 位有效）时，旧的 `advance_word` 判定不会在第一次
读取后推进 FIFO 指针，下一次读取会返回同一 beat 的无效高 16 位。源图和目标图
地址低两位不同时，sprite 的两个 FIFO 会因此发生像素错位；colorfill 不使用配对
源/目标读取，BLT 也不经过该同步消费路径，所以两者可正常工作。

已将推进条件改为：读取低 lane 时只要高 lane 无效就立即推进；读取高 lane 时
始终推进；`keep=11` 仍保留指针供第二个像素读取。该修改只影响 FIFO lane 消费，
不改变 AXI 仲裁、parser、writer 或显示切帧逻辑。需要在当前 sprite 工程上重新
编译并验证未对齐源/目标地址和最后一个单像素 beat。

## Sprite lane 修复补充（2026-10-30）

复核发现原推进条件对 `keep=10` 仍会重复读取高 16 位：`rd_lane=0` 时
`rd_data` 已选择唯一有效的高 lane，但 `advance_word` 却只检查高 lane 是否无效，
导致同一 FIFO word 被再次读取。现改为低 lane 无效或高 lane 无效时，在一次有效
读取后立即退休当前 word；只有 `keep=11` 的第一次低 lane 读取保留 word，第二次
读取高 lane 后再推进。这样同时覆盖 `keep=01`、`keep=10` 和 `keep=11`，避免
sprite 源/目标 FIFO 在行边界或半字偏移下错位。

验证：`sim/tb_gpu_sync_fifo_lane.v` 使用 ModelSim 检查输出顺序
`AAAA, DDDD, EEEE, FFFF`，仿真通过。

## Heavy sprite/BLT black lower region investigation (2026-10-30)

大量 writer burst 连续提交时，HDMI 读 FIFO 可能在活动行期间欠载。`ddr3_soc_top`
在 `empty_o` 或复位期间会把 HDMI 像素数据输出为黑色，因此画面下半部分变黑
并不表示 framebuffer 中该区域被写成黑色，而是 HDMI scanout 没有拿到数据。
当前 writer 在每个非末尾 burst 收到 `B` 后立即进入下一次准备流程。现由
`axi_rgb565_dma8` 在 `wburst_done` 后清空 `write_owner`，并保持 8 个
`user_clk` 周期不授予新的 GPU 写 owner，确保 HDMI DDR 读 master 获得真实的
仲裁窗口；writer 的 AXI 数据格式、地址推进和 `burst_done` 语义不变，末尾
burst 仍直接报告 `done`。代价是 GPU 写吞吐下降，硬件验证后可将该计数调为 4。



## 2026-10-30 — 64-bit reader/FIFO 数据损坏修复

工程固定为 `D:/26FPGA/Ti60F225_demo_new`。本次只改 `rtl/GPU/axi_rgb565_reader.v`
和 `rtl/GPU/gpu_sync_fifo.v`，没有修改 HDMI、writer、parser 或 RISC-V 软件。

确认的迁移错误：
1. 4:1 FIFO 的读 lane 推进仍是 `rd_lane <= 1`，完整 word 读出 lane0 后
   持续重复 lane1，而像素计数仍下降。BLT/sprite 的源/目标数据因此损坏。
2. reader 已将未对齐数据右移到低位，但 keep 仍按原始高位偏移生成，
   导致有效标记和实际像素不对应。现按有效字节数产生 0001/0011/0111/1111。
3. FIFO 满判断仍按 2:1 计算；部分有效 word 只计有效像素，不能阻止物理 RAM
   word 被绕回覆盖。现额外统计实际占用的 RAM entries，零 keep 不入队；
   保留 DEPTH 的像素容量定义和原 RAM 深度，部分 word 会占用完整 RAM entry。
4. popcount 改成按参数遍历，避免等宽或 2:1 实例访问不存在的 keep[2]/keep[3]。
   默认 reader BURST_LEN 修正为 8；保留原多 AR 描述符机制。

ModelSim 实测通过（不是仅语法检查）：
- `sim/reader64_fix/tb_reader64.v`：reader 连接 64->16 FIFO，34 组逐像素比对；
  覆盖 byte offset 0/2/4/6、32x32、宽 1..7 的行尾、454-byte stride、
  4KB 边界、1280 像素行、多 outstanding 和 R 反压；11614 个反压周期。
- `tb_fifo_random.v`：1:1 / 2:1 / 4:1 各 5000 周期随机读写、keep mask、
  同拍读写、指针回绕，通过；分别检查 3692/3581/3760 个输出。
- 原 `sim/tb_gpu_sync_fifo_lane.v` 的 AAAA/DDDD/EEEE/FFFF 回归通过。
- GPU RTL 编译及 GPU_top 展开通过；存在原有未连接输出端口警告。

复现：在 `sim/reader64_fix` 运行 `./run.ps1`。脚本分别启动三个仿真，
检查日志 PASS 和 Fatal/Error；默认 ModelSim 路径可由 ModelSimBin 参数覆盖。
日志与测试保存在该目录。本次没有重新布局布线或上板，不能据此确认最终
core_clk Fmax、DDR 控制器实测行为或屏幕效果；需重新生成 bitstream 验证。


## 2026-10-31 — 四槽源图缓存与 FCFS

实际工程：`D:/26FPGA/Ti60F225_demo_new`。用户创建的 IP 名称为 `GPU_CACHE`，
位于 `par/ddr_demo_ti60/ip/GPU_CACHE/`，SP_RAM，256 x 64 bit，输出不额外寄存。
`rtl/GPU/gpu_source_cache.v` 例化四个原有 IP，每槽 1024 个 RGB565 像素，
总逻辑数据容量 8 KiB。使用真实 IP 与 EFX_RAM10 仿真模型验证同步读延迟。

指令编码与行为：
- color[31] 是 cachein；color[30:28] 为缓存编号。适用于 OP=0/2/3/5。
- 编号 0、5、6、7：硬件忽略缓存设置，正常 DDR 源图读取。
- cachein=1 且编号 1..4：正常渲染，同时把源 reader 的有效像素按顺序压紧
  存入对应 RAM；只存前 1024 个，超出部分继续正常渲染，不覆盖已存前缀。
- cachein=0 且编号 1..4：命中时直接读取 RAM 到源 FIFO，无源图 AR 请求。
  保留填充时的行宽；较小尺寸读取缓存图片左上角区域，每行按缓存行宽推进。
  命中时 DDR 源地址/stride 不用于选择缓存中的偏移，不支持任意子矩形原点。
- 未填充或要求区域超出已保存前缀时，整批源读取自动回退 DDR，不访问未初始化 RAM。
- sprite/alpha/alphasprite 仍正常读取目标 framebuffer；缓存保存混合前的源像素。
- 分批读取保持同一槽的计数与末尾不满四像素的打包状态，不按 batch 重置。
- 硬件复位清除有效位；更新源图后需要重新 cachein，缓存不自动侦测 DDR 内容变化。

调度与握手：
- `gpu_schduler8` 按原命令队列顺序派发；相同编号访问使用每核槽位占用记录，
  后到命令在 S_SEND 等待。占用从派发开始，覆盖 parser 尚未拉高 busy 的交接拍。
- 最后一个源 batch 的 read_done 后释放槽位，不必等 writer 完成，下一条缓存读取
  可以与前一条写回并行。异常/命令结束也释放记录。队首冲突会等待，不越序派发。
- 第一版共用一个源缓存服务通路，四个 RAM 用于四张常驻源图；没有实现四槽同时输出。
  延续现有读 owner 仲裁，缓存命中期间不发 AXI AR，但仍占用这个逻辑读 owner。
- 缓存命中读出为一拍同步 RAM 流水；无反压时每拍一个 64-bit 字，最多四个像素。
  RAM 数据与 lane/keep/last 同步停顿。行尾不足四像素时按实际有效数送 FIFO。
- cachein 为正确压紧未对齐与不完整行，在接受 reader beat 后逐像素打包；首次填充
  有额外反压开销，主要收益来自后续重复命中。没有测量板上吞吐提升倍率。
- 缓存元数据选择、行偏移乘法、边界加法/判定分阶段寄存，避免串成单拍长通路。
- 修复 scheduler_busy 遗漏 parser_cmd_valid 的一拍空隙，避免派发期间错误报空闲。
- writer 既有 6 周期强制空窗、当前 GPU_top 默认 CORE_COUNT=3 均保留。

涉及文件：
`rtl/GPU/gpu_source_cache.v`、`axi_rgb565_dma.v`、`bitblt_cmd_parser.v`、
`gpu_schduler.v`、`GPU_top.v`，以及两个工程 XML 的源文件/IP 引用。
新增引用使用相对路径。原 `GPU_CACHE` IP 文件没有改动。
`HDMI_Stress/src/2D_DMA.c` 的 CacheOP 从逻辑或 `||` 改为位或 `|`，
否则无论指定哪一槽都会得到数值 1，无法传入 color 高四位。
表达式为 `((u32)(cachein & 1u) << 31) | ((u32)(cachenum & 7u) << 28)`。

示例：
- 第一次 `pic_blt(..., frame, 1, 1)`：DDR 读取并填充槽 1。
- 后续 `pic_blt(..., frame, 0, 1)`：复用槽 1；请求尺寸可等于或小于原图。
- alpha=16 时，槽 1 填充/复用的 color 分别为 0x90000010/0x10000010。
- 复用不同内容前重新填充槽位。低 16 位颜色键/alpha 参数继续保持原含义。

验证：
`sim/gpu_cache/run.ps1` 使用 ModelSim 编译真实 GPU_CACHE IP 和厂商 EFX_RAM10
模型，驱动 GPU_top 的 CPU AXI 命令接口及 DDR AXI 内存模型，逐像素检查结果。
通过：四槽、20x20→13x11 裁剪、32x32 满容量、未对齐/奇数宽尾部、1025 像素
跨批只留 1024、1280 像素单行前缀、1x80 跨批复用、空槽/无效编号/越界回退、
四种指令命中源 AR=0、混合指令填充的仍为源图、同编号连续覆盖后读取、FIFO 反压、
colorfill 与 OP=4 路径回归。四核测试中观察到 1305 个周期的多核 busy 重叠，
缓存连续 64-bit 输出最长 276 拍。日志 `sim/gpu_cache/integration.log`。
HDMI_Stress 的 2D_DMA.c 已用项目 RISC-V GCC 与对应 BSP 参数编译通过。
未运行 Efinity 布局布线和上板，Fmax、资源用量和实际吞吐待实测。

OP=4 回归备注：当前原版 parser 的 scale_row_y_reg 每行加 scale_q26，随后直接
乘 src_stride；例如 scale_q26=64 时源行地址步进 64*stride。这在本次修改前的
备份仿真也存在（`sim/gpu_cache/baseline_scale.log`），本次保持原有地址行为，
没有把该回归通过解释为缩放算法本身正确。
本次修改前的文件备份在 `sim/gpu_cache/before_cache/`，未使用 git。

## 2026-10-31 cached SPRITE versus uncached BLT underrun investigation

Production RTL and software unchanged in this investigation. Actual project is D:/26FPGA/Ti60F225_demo_new. No git used.

Current HDMI_Stress uses 32x32 sprite, source width 227 (454-byte pitch), crop (100,50), framebuffer pitch 2560, cache bank 2, warmup cachein=1 followed by cachein=0. Current delay is 9 us. Frame submission waits bitblt_wait; do not diagnose this as merely accepted versus completed commands. Warmup return is not checked separately.

Added sim/gpu_cache/tb_cache_pressure.sv and pressure.log. Recompiled current RTL through run.ps1; full existing cache integration regression passed. Comparison uses 3 cores, 12 commands/case, source offset 22900 and pitch 454, moving aligned targets with pitch 2560. Addresses are relocated into a 2 MiB simulation memory. Deterministic AXI backpressure model, NOT the SoC/physical DDR or HDMI model; submission is saturated, not the software's 9 us pacing. No hardware underrun reproduction claimed.

Results (64-bit data beats):
- Uncached BLT: cycles 21924, AR 672, R 3360, AW 384, W 3072, peak outstanding 33, RVALID&&!RREADY 0.
- Cached SPRITE: cycles 24996, AR 384, R 3072, AW 384, W 3072, peak outstanding 25, RVALID&&!RREADY 0.
- Uncached SPRITE: cycles 25764, AR 1056, R 6432, AW 384, W 3072, peak outstanding 33, RVALID&&!RREADY 0.

Per command: uncached BLT 56 AR / 280 R beats (2240 read bytes); cached SPRITE 32 AR / 256 R beats (2048 read bytes); both 32 AW / 256 W beats (2048 write bytes). Source stride alignment causes BLT extra partial bursts. Thus no evidence of duplicate source reads on cache hits or extra sprite write traffic. Model cached sprite time is only about 14% above BLT, insufficient to explain the reported large underrun threshold difference. Cycle results are model-specific, not board throughput predictions.

Verified differences: BLT reads the same immutable source crop; cached SPRITE reads moving destination framebuffer rows, then writes that region. Target read is unconditional, even for opaque source pixels. Cache replay currently occupies the internal GPU read owner, preventing concurrent GPU destination reads during replay; without ARVALID this does NOT hold the external SoC AR arbiter. Source and destination arbitration/phase timing differ; physical DDR row/bank effects are hypotheses, not measured facts.

Correction to earlier source discovery: StreamArbiter_1 DOES exist in par/ddr_demo_ti60/ip/soc/source/hardware/netlist/source_efinity/EfxSapphireSoc.v at line 26619. Search generated files explicitly or with --no-ignore. It uses round-robin request selection and locks under backpressure; QoS is forwarded but not used in its selection. It has no HDMI FIFO water-level input. This explains why raising QoS cannot change this local arbiter, though downstream treatment is a separate question.

HDMI axi_frame_reader has only one outstanding request (AR -> RLAST -> next AR), versus GPU batch request queue. Round robin at AR admission does not guarantee HDMI response deadlines or move it ahead of already accepted GPU reads. At 720p pixel clock 74.25 MHz, 32 pixels only cover about 0.431 us of active video. ddr3_soc_top clears hdmi_frame_valid on any active-video empty_o/rst_busy and keeps the rest of that frame black. This verified behavior amplifies a short service interruption into a large missing lower region; it is not evidence of framebuffer corruption.

Next discriminating measurements: HDMI underflow count/min FIFO level/maximum own AR-to-first-R and RLAST-to-next-AR gap; GPU source versus destination AR counters and outstanding peak under the same on-board workload. Compare uncached SPRITE, cached SPRITE and uncached BLT at matched completed command rate and positions, with warmup success checked. A bounded destination-reader outstanding limit (e.g. 4/8 versus 32), retaining internal prefetch, is a diagnostic experiment rather than a proven final fix. For a durable solution consider HDMI multiple outstanding refill or water-level admission control that acts before the DDR queue fills; QoS-only and average-byte-bandwidth arguments are insufficient. Do not change writer or sprite transparency semantics without evidence.

## 2026-10-31 HDMI read-address priority implemented

Modified only par/ddr_demo_ti60/ip/soc/source/hardware/netlist/source_efinity/EfxSapphireSoc.v for production. StreamArbiter_1 is shared by AW and AR arbiters, so added HDMI_PRIORITY parameter default 0 and enabled it ONLY in Axi4ReadOnlyArbiter. Verified mapping input0 CPU, input1 userAdapters_0/external ddrMasters_1 GPU, input2 userAdapters_1/external ddrMasters_0 HDMI.

When unlocked: choose HDMI if valid; otherwise CPU/GPU round robin. CPU/GPU preference advances only upon their successful AR handshake; HDMI grants do not change it. Existing locked/maskLocked logic retains selected request and all payload fields under backpressure until handshake. Write arbiter retains original round robin; GPU writer 6-cycle gap unchanged. Already accepted DDR requests cannot be preempted by this policy.

Backup, exact extracted test module, testbench, diff and logs are in sim/hdmi_priority. ModelSim test passed 10000 cycles with directed and randomized contention, stalled GPU followed by HDMI, onehot ready/chosen, and all AR payload fields checked (CPU/GPU/HDMI grants 973/960/2716). Full generated SoC compile attempted but blocked by encrypted regions without matching ModelSim keys; standalone modified arbiter compiled and simulated successfully. Efinity synthesis/PnR and board underrun validation remain pending. Regenerate bitstream before board test; no RISC-V change needed.

## 2026-10-31 CORRECTION: actual synthesis input is ip/soc/soc.v

The 16:30:36 outflow/Ti60F225_demo_new.map.rpt explicitly lists ip/soc/soc.v (line 39), using ddr_demo_ti60.xml. The previous source_efinity/EfxSapphireSoc.v change was NOT in that synthesis input. Earlier statements that it was the synthesis RTL were incorrect. The old board result therefore does not establish that HDMI priority failed.

Applied the same verified policy to ip/soc/soc.v, whose module names carry suffix _67570fc7a21c4cb18855a1f2efcac4bb. Confirmed its arbiter was byte-for-byte logically identical to the original after normalizing the module name. Read instance enables HDMI_PRIORITY at line15524; definition at27093, selection logic27234. Write instance remains default. Preserved file encoding/line endings and unrelated content. Backup sim/hdmi_priority/soc.v.before; patch soc_change.diff. Re-extracted actual patched module (only name normalized for test) and passed the 10000-cycle ModelSim regression again (actual_soc_result.log). No synthesis/PnR run performed in this correction. Must rerun synthesis then PnR/bitstream, not PnR-only reuse of the old VDB.

## 2026-10-31 priority build verified, underrun persists

Latest proj_state.ini reports pass/bitstream. outflow/Ti60F225_demo_new.map.v contains the cpu_gpu_next EFX_FF under system_ddr_ddrLogic_arbiterAxi4Read/cmdArbiter, attributed to ip/soc/soc.v line27245. This confirms the new read arbiter was synthesized, not merely edited on disk. User reports unchanged underrun with the new run. Do not keep diagnosing missing compilation.

DDR axi4_bus_ctl.v implementation after its interface is encrypted, as is memory_bus_ctl.v; cannot establish DDR internal read/write priority from visible source. AR ingress priority cannot reorder previously accepted GPU requests nor prioritize reads over independent AW/W traffic. HDMI still has only one read outstanding. Next useful diagnostic is separate HDMI ARVALID&&!ARREADY latency, AR-to-first-R and in-burst R gaps, with FIFO low/empty events. Candidate mitigation is FIFO-low-driven admission of NEW GPU AR/AW transactions (with CDC and hysteresis), never withdrawing a stalled VALID or blocking completion W/B/R. Reserve headroom for already outstanding GPU traffic. This is proposed, not implemented or proven necessary.

## 2026-10-31 source/destination reader differential check

Inspected axi_rgb565_dma.v reader/dst_reader wiring, GPU_top.v paired FIFO pops, parser read completion handling. Both reader instances use the same module, BURST_LEN and DESC_DEPTH=32. Target RVALID/RREADY and ARREADY are gated with its target owner; target FIFO empty joins source empty before paired consumption. No wiring discrepancy established. Source path has cache wrapper and source owner remains occupied during cache replay; destination path writes FIFO directly. These are genuine phase/latency differences, not proof of underrun cause.

Added sim/gpu_cache/tb_cache_paths.sv. Recompiled current RTL via run.ps1; pixel integration regression passes. Added assertion that read owner cannot change with outstanding accepted reads; passed. BFM remains deterministic and lacks physical DDR/HDMI, so cannot prove real-time HDMI service or reproduce the board failure.

12-command comparison, 3 cores, 32x32:
- ordinary BLT source pitch454: 21924 cycles,672AR,3360R,384AW,3072W,peak33,Rstall0.
- BLT reading the same moving framebuffer addresses as cached SPRITE (pitch2560, in-place):21668 cycles,384AR,3072R,384AW,3072W,peak25,Rstall0.
- cached SPRITE:24996 cycles,384AR,3072R,384AW,3072W,peak25,Rstall0.

Thus matched-address BLT and cached SPRITE have identical transfer counts and peak outstanding in this model; cached SPRITE is ~15.4% slower. No evidence that extra read volume, bad owner handoff or RREADY stalls explains the severe board disparity. Model timings are not DDR predictions. Board comparison with matched addresses, pacing and completed counts plus actual HDMI empty counters is still needed. Do not claim resolved or apply global throttling based solely on these results. No production RTL/software edited in this check.

## Cache replay releases AXI read owner

Changed rtl/GPU/gpu_source_cache.v to expose replay_active only in confirmed RAM_READ hit state; axi_rgb565_dma.v releases that cache core's source read owner when asserted. Prevented re-grant of unaccepted source commands while cache controller is busy. Destination readers can then run concurrently with RAM replay, including the same core. Cache completion cannot release a destination owner. Miss/fill DDR paths retain original ownership. Shared source cache controller still serializes source services: this change permits destination DDR reads during replay, not another source service through the occupied controller. Writer unchanged.

Current RTL cache pixel regression passed. Extended tb_cache_paths owner assertion and replay/destination overlap check passed (see replay_owner.log). Cached SPRITE 12-command model cycles fall from24996 to21872, with unchanged384AR/3072R/384AW/3072W and zero R stalls; this is model speedup ~14.3%, not a board performance/underrun guarantee. Backups: sim/gpu_cache/replay_owner_before. Requires synthesis/PnR and board validation.

## 2026-11-01 cache expanded from four to seven slots

rtl/GPU/gpu_source_cache.v now instantiates seven GPU_CACHE RAMs, each unchanged at1024 RGB565 pixels. Slot IDs1..7 enabled;0 bypasses even with cachein=1. Internal bank index widened to3 bits; metadata/valid bitmap expanded to7 entries. Disabled slot maps internal index to0 to avoid out-of-range metadata accesses, while use/fill remain disabled. rtl/GPU/gpu_schduler.v accepts1..7 for same-bank FCFS hazards. Existing 3-bit software flags already support7, so no software change needed. Replay owner release retained.

Total logical pixel storage14KiB (previous8KiB), increase6KiB/three IP RAM instances; actual FPGA block usage and timing require synthesis/PnR. Existing tests updated for all seven slots, slot0 bypass, and back-to-back same-slot overwrite/read ordering specifically on slot7. Full ModelSim pixel regression passed. Backups in sim/gpu_cache/before_seven_slots. No SoC, writer or HDMI changes.

## 2026-11-01 HDMI FIFO and multi-AR prefetch

Changed rtl/axi_frame_reader.v to maintain a 16-entry burst descriptor queue. It can issue up to16 AR bursts ahead while the FIFO has space; R responses are consumed in descriptor order and each RLAST retires the head. AR payload address/length advance on AR handshake. RREADY remains low when FIFO full. A one-cycle mutual exclusion avoids simultaneous AR and final R descriptor counter updates. This preserves AXI VALID stability and does not cross 4KiB boundaries because calc_burst_beats is retained.

Changed par/ddr_demo_ti60/ip/HDMI_fifo/HDMI_fifo.sv from DEPTH1024 to2048 write-side 64-bit words, PROG_FULL assert/negate 1920, and count outputs to12-bit write/13-bit read. Changed rtl/ddr3_soc_top.v count wires accordingly; frame-start threshold remains1280 pixels with13-bit comparison. The FIFO now stores roughly8192 RGB565 pixels (about16KiB payload; exact vendor accounting depends on asymmetric FIFO implementation), doubling the previous capacity.

ModelSim syntax compile of axi_frame_reader.v and HDMI_fifo.sv passed. Full SoC/IP synthesis and FIFO primitive capacity support are still pending; rebuild from synthesis and verify timing/resource usage. Multi-AR increases outstanding DDR traffic, so HDMI priority and FIFO depth should be evaluated together on board.

2026-11-01: no_physical_HDMI_ACK appeared after multi-AR reader. Root risk is top-level frame_read_burst_active is a single bit while reader has multiple ARs in flight; it clears on first RLAST. Restored axi_frame_reader.v to stable single AR->R burst state machine. FIFO remains DEPTH1024, PROG_FULL896. This restores compatibility with frame drain/ACK logic; multi-AR requires top-level outstanding counter before retry.

## 2026-11-01 eight-outstanding HDMI reader/top drain counter

Implemented axi_frame_reader MAX_OUTSTANDING=8 with an 8-entry descriptor length queue. AR issue address and remaining bytes advance only on AR handshake. RREADY requires a queued descriptor and non-full FIFO. RLAST validates the head descriptor and advances head; descriptor count is updated once per cycle from {AR_FIRE, RLAST_FIRE}, including simultaneous issue/retire. done is asserted only when the final descriptor retires and no new AR is issued. Four-KiB boundary calculation retained.

Updated rtl/ddr3_soc_top.v frame_read_burst_active to a derived 4-bit frame_read_burst_count. AR handshake increments, RLAST handshake decrements; simultaneous AR/RLAST leaves count unchanged. DRAIN waits count==0 and frame_ar_enabled==0. frame_ar_enabled/ARVALID is held through a stalled already-admitted AR during a ping-pong request, so a valid request is not withdrawn before handshake. New ARs are blocked by ping-pong state after the admitted request completes.

axi_frame_reader.v syntax compile passes. The exploratory BFM test was corrected to model per-burst RLAST and backpressure; run it before relying on it as a functional proof. Full SoC synthesis/PnR and physical ACK validation remain required.

## 2026-11-01 cache slots restored to four

Restored cache implementation from seven slots to four: gpu_source_cache arrays/valid bitmap/RAM instances/initialization and allowed IDs1..4; gpu_schduler same-bank cache enable accepts IDs1..4. Slot0 and IDs5..7 bypass cache. Updated cache integration test to cover four banks and invalid IDs. Regression passes all GPU cache integration checks. HDMI reader remains the eight-AR version and top-level outstanding counter remains enabled.

## 2026-11-01 cache re-expanded to seven from verified four-slot baseline

Re-expanded gpu_source_cache.v from the known-good four-slot version to seven slots: arrays/valid bitmap/RAM data widened to indices0..6, generate loop creates seven GPU_CACHE instances, bank remains3 bits, and allowed cache IDs are explicitly1..7. gpu_schduler.v same-bank FCFS cache hazard accepts IDs1..7. Cache ID0 remains bypass. Updated integration test covers slots2..7 and invalid-ID behavior. Full ModelSim run.ps1 regression passed all checks including pixels, cache-hit zero source AR, FCFS, multi-core overlap, OP4 regression, and streaming. This is a clean reimplementation; no HDMI/reader changes in this step. Requires synthesis/PnR resource/timing validation.

## 2026-11-01 zoom_test hang investigation/fix

Demoshow zoom_test() ignored pic_zoom() return and did not wait for bitblt completion. It could continue frame submission while OP4 was still busy or silently ignore -2/-11..-14. Added size clamp0.25..4.0, retry on -2, and bitblt_wait after successful pic_zoom.

OP4 sampler found a real completion bug: gpu_scale_x_sampler line_done required line_active to remain true, but line_active is cleared on the final emitted pixel, so line_done could never assert after the last output. Fixed line_done to pulse on the final emit or the normal drained state. Also changed source latch condition from exact src_index==wanted_index to src_index>=wanted_index so skipped source indices cannot leave the sampler waiting forever. ModelSim sampler test now passes: scale64,dst_width32, reads32, outputs32, line_done asserted. Production files changed: rtl/GPU/gpu_scale_x_sampler.v and demo_Show/src/HDMI_show.c. No writer/cache changes.
