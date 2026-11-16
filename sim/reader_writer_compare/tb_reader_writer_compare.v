`timescale 1ns/1ps

module tb_reader_writer_compare;
    reg clk = 1'b0;
    always #2.5 clk = ~clk; // 200 MHz

    integer failures;
    integer r_cycles, r_beats, r_pixels, r_ar, r_yield, r_done;
    integer r_first_latency_sum, r_first_latency_max;
    integer r_gap_max, r_stall_cycles, r_yield_to_ar_sum, r_yield_to_ar_max;
    integer r_cycle;
    integer r_ar_cycle, r_first_ar_cycle, r_last_beat_cycle, r_yield_cycle;
    integer r_first_pending, r_yield_pending;
    integer r_ar_count, r_gap_count, r_yield_ar_count;
    reg [31:0] r_cmd_length;
    integer w_cycles, w_fill, w_aw, w_w, w_b, w_done;
    integer read_latency;
    integer b_delay;

    reg rst_n;

    // Reader signals
    reg r_cmd_valid;
    reg [31:0] r_cmd_stride;
    reg [6:0] r_cmd_rows;
    wire r_cmd_ready, r_busy, r_done_sig, r_error;
    wire r_bus_request, r_bus_yield;
    reg r_fifo_full;
    wire r_fifo_wr_en;
    wire [31:0] r_fifo_wr_data;
    wire [1:0] r_fifo_wr_keep;
    wire r_fifo_wr_last;
    wire [3:0] r_arid;
    wire [31:0] r_araddr;
    wire [7:0] r_arlen;
    wire [2:0] r_arsize;
    wire [1:0] r_arburst;
    wire [3:0] r_arcache, r_arqos, r_arregion;
    wire [2:0] r_arprot;
    wire r_arlock, r_arvalid;
    reg r_arready;
    reg [3:0] r_rid;
    reg [31:0] r_rdata;
    reg [1:0] r_rresp;
    reg r_rlast, r_rvalid;
    wire r_rready;
    integer r_pending_delay, r_pending_beats;
    reg r_active;
    // Match the reader's maximum outstanding burst window. The real AXI
    // bridge must accept the same-ID queue used by the 32-entry descriptor
    // queue; keeping this model at four falsely reports overflow.
    localparam integer RESP_QUEUE_DEPTH = 32;
    integer r_queued_delay [0:RESP_QUEUE_DEPTH-1];
    integer r_queued_beats [0:RESP_QUEUE_DEPTH-1];
    integer r_queue_wr, r_queue_rd, r_queue_count;

    axi_rgb565_reader #(.BURST_LEN(16), .DESC_DEPTH(`READER_DESC_DEPTH)) u_reader(
        .clk(clk), .rst_n(rst_n), .cmd_valid(r_cmd_valid), .cmd_ready(r_cmd_ready),
        .cmd_addr(32'h0010_0000), .cmd_length_bytes(r_cmd_length),
        .cmd_stride_bytes(r_cmd_stride), .cmd_rows(r_cmd_rows),
        .busy(r_busy), .done(r_done_sig), .error(r_error),
        .bus_request(r_bus_request), .bus_yield(r_bus_yield),
        .pause_new_bursts(1'b0), .fifo_full(r_fifo_full),
        .fifo_wr_en(r_fifo_wr_en), .fifo_wr_data(r_fifo_wr_data),
        .fifo_wr_keep(r_fifo_wr_keep), .fifo_wr_last(r_fifo_wr_last),
        .m_axi_arid(r_arid), .m_axi_araddr(r_araddr), .m_axi_arlen(r_arlen),
        .m_axi_arsize(r_arsize), .m_axi_arburst(r_arburst), .m_axi_arcache(r_arcache),
        .m_axi_arqos(r_arqos), .m_axi_arregion(r_arregion), .m_axi_arprot(r_arprot),
        .m_axi_arlock(r_arlock), .m_axi_arvalid(r_arvalid), .m_axi_arready(r_arready),
        .m_axi_rid(r_rid), .m_axi_rdata(r_rdata), .m_axi_rresp(r_rresp),
        .m_axi_rlast(r_rlast), .m_axi_rvalid(r_rvalid), .m_axi_rready(r_rready)
    );

    // Writer signals
    reg w_cmd_valid;
    wire w_cmd_ready, w_bus_request, w_burst_done, w_busy, w_done_sig, w_error;
    wire w_fifo_ready;
    reg w_fifo_valid;
    reg [15:0] w_fifo_data;
    reg w_bus_grant;
    wire [3:0] w_awid;
    wire [31:0] w_awaddr;
    wire [7:0] w_awlen;
    wire [2:0] w_awsize;
    wire [1:0] w_awburst;
    wire [3:0] w_awcache, w_awqos, w_awregion;
    wire [2:0] w_awprot;
    wire w_awlock, w_awvalid;
    reg w_awready;
    wire [31:0] w_wdata;
    wire [3:0] w_wstrb;
    wire w_wlast, w_wvalid;
    reg w_wready;
    reg [3:0] w_bid;
    reg [1:0] w_bresp;
    reg w_bvalid;
    wire w_bready;
    integer w_b_countdown;
    wire resp_rlast_fire = r_active && r_rvalid && r_rready && r_rlast;

    axi_rgb565_writer #(.BURST_LEN(16)) u_writer(
        .clk(clk), .rst_n(rst_n), .cmd_valid(w_cmd_valid), .cmd_ready(w_cmd_ready),
        .cmd_addr(32'h0020_0000), .cmd_length_bytes(32'd64),
        .bus_request(w_bus_request), .bus_grant(w_bus_grant),
        .burst_done(w_burst_done), .busy(w_busy), .done(w_done_sig), .error(w_error),
        .fifo_valid(w_fifo_valid), .fifo_ready(w_fifo_ready), .fifo_data(w_fifo_data),
        .m_axi_awid(w_awid), .m_axi_awaddr(w_awaddr), .m_axi_awlen(w_awlen),
        .m_axi_awsize(w_awsize), .m_axi_awburst(w_awburst), .m_axi_awcache(w_awcache),
        .m_axi_awqos(w_awqos), .m_axi_awregion(w_awregion), .m_axi_awprot(w_awprot),
        .m_axi_awlock(w_awlock), .m_axi_awvalid(w_awvalid), .m_axi_awready(w_awready),
        .m_axi_wdata(w_wdata), .m_axi_wstrb(w_wstrb), .m_axi_wlast(w_wlast),
        .m_axi_wvalid(w_wvalid), .m_axi_wready(w_wready), .m_axi_bid(w_bid),
        .m_axi_bresp(w_bresp), .m_axi_bvalid(w_bvalid), .m_axi_bready(w_bready)
    );

    always @(posedge clk) begin
        if (!rst_n) begin
            r_arready <= 1'b0; r_rvalid <= 1'b0; r_rlast <= 1'b0;
            r_pending_delay <= 0; r_pending_beats <= 0; r_active <= 1'b0;
            r_queue_wr <= 0; r_queue_rd <= 0; r_queue_count <= 0;
            r_cycle <= 0;
        end else begin
            r_cycle <= r_cycle + 1;
            r_arready <= 1'b1;
            if (r_queue_count > 0 && r_queued_delay[r_queue_rd] > 0)
                r_queued_delay[r_queue_rd] <= r_queued_delay[r_queue_rd] - 1;
            if (r_arvalid && r_arready) begin
                if (!r_active) begin
                    r_active <= 1'b1;
                    r_pending_delay <= read_latency;
                    r_pending_beats <= r_arlen + 1;
                end else if (resp_rlast_fire && r_queue_count == 0) begin
                    r_pending_delay <= read_latency;
                    r_pending_beats <= r_arlen + 1;
                end else if (r_queue_count < RESP_QUEUE_DEPTH) begin
                    r_queued_delay[r_queue_wr] <= read_latency;
                    r_queued_beats[r_queue_wr] <= r_arlen + 1;
                    r_queue_wr <= (r_queue_wr + 1) % RESP_QUEUE_DEPTH;
                    r_queue_count <= r_queue_count + 1;
                end else begin
                    $display("ERROR: responder queue overflow");
                    failures = failures + 1;
                end
            end
            if (r_active) begin
                if (r_rvalid) begin
                    if (r_rready) begin
                        if (r_pending_beats == 1) begin
                            r_rvalid <= 1'b0;
                            r_rlast <= 1'b0;
                            if (r_queue_count > 0) begin
                                r_pending_delay <= r_queued_delay[r_queue_rd];
                                r_pending_beats <= r_queued_beats[r_queue_rd];
                                r_queue_rd <= (r_queue_rd + 1) % RESP_QUEUE_DEPTH;
                                if (r_arvalid && r_arready) begin
                                    r_queued_delay[r_queue_wr] <= read_latency;
                                    r_queued_beats[r_queue_wr] <= r_arlen + 1;
                                    r_queue_wr <= (r_queue_wr + 1) % RESP_QUEUE_DEPTH;
                                    r_queue_count <= r_queue_count;
                                end else begin
                                    r_queue_count <= r_queue_count - 1;
                                end
                                r_active <= 1'b1;
                            end else if (r_arvalid && r_arready) begin
                                r_pending_delay <= read_latency;
                                r_pending_beats <= r_arlen + 1;
                                r_active <= 1'b1;
                            end else begin
                                r_pending_beats <= 0;
                                r_active <= 1'b0;
                            end
                        end else begin
                            r_pending_beats <= r_pending_beats - 1;
                            r_rvalid <= 1'b1;
                            r_rdata <= 32'hA500_0000 | (r_pending_beats - 1);
                            r_rlast <= (r_pending_beats == 2);
                        end
                    end
                end else if (r_pending_delay != 0) begin
                    r_pending_delay <= r_pending_delay - 1;
                end else if (r_pending_beats != 0) begin
                    r_rvalid <= 1'b1;
                    r_rdata <= 32'hA500_0000 | r_pending_beats;
                    r_rlast <= (r_pending_beats == 1);
                end
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            w_bvalid <= 1'b0; w_b_countdown <= -1;
        end else begin
            w_bvalid <= 1'b0;
            if (w_wvalid && w_wready && w_wlast)
                w_b_countdown <= b_delay;
            else if (w_b_countdown >= 0) begin
                if (w_b_countdown == 0) begin
                    w_bvalid <= 1'b1;
                    if (w_bready) w_b_countdown <= -1;
                end else w_b_countdown <= w_b_countdown - 1;
            end
        end
    end

    always @(posedge clk) begin
        if (rst_n) begin
            if (r_busy) r_cycles = r_cycles + 1;
            if (r_fifo_wr_en) r_beats = r_beats + 1;
            if (r_fifo_wr_en) r_pixels = r_pixels + r_fifo_wr_keep[0] + r_fifo_wr_keep[1];
            if (r_arvalid && r_arready) begin
                r_ar = r_ar + 1;
                r_ar_count = r_ar_count + 1;
                if ((r_cmd_length == 64 || r_cmd_length == 32) &&
                    (r_araddr != (32'h0010_0000 + ((r_ar_count-1) * r_cmd_stride)) ||
                     r_arlen != ((r_cmd_length == 32) ? 8'd7 : 8'd15))) begin
                    $display("ERROR: bad batch AR #%0d addr=%08x len=%0d",
                             r_ar_count, r_araddr, r_arlen);
                    failures = failures + 1;
                end
                r_ar_cycle = r_cycle;
                if (r_ar_count == 1) begin
                    r_first_ar_cycle = r_cycle;
                    r_first_pending = 1;
                end
                if (r_yield_pending) begin
                    if ((r_cycle - r_yield_cycle) > r_yield_to_ar_max)
                        r_yield_to_ar_max = r_cycle - r_yield_cycle;
                    r_yield_to_ar_sum = r_yield_to_ar_sum + (r_cycle - r_yield_cycle);
                    r_yield_ar_count = r_yield_ar_count + 1;
                    r_yield_pending = 0;
                end
            end
            if (r_bus_yield) begin
                r_yield = r_yield + 1;
                r_yield_cycle = r_cycle;
                r_yield_pending = 1;
            end
            if (r_rvalid && !r_rready)
                r_stall_cycles = r_stall_cycles + 1;
            if (r_rvalid && r_rready) begin
                if (r_first_pending) begin
                    if ((r_cycle - r_first_ar_cycle) > r_first_latency_max)
                        r_first_latency_max = r_cycle - r_first_ar_cycle;
                    r_first_latency_sum = r_first_latency_sum + (r_cycle - r_first_ar_cycle);
                    r_first_pending = 0;
                end else if (r_gap_count != 0) begin
                    if ((r_cycle - r_last_beat_cycle - 1) > r_gap_max)
                        r_gap_max = r_cycle - r_last_beat_cycle - 1;
                end
                r_last_beat_cycle = r_cycle;
                r_gap_count = r_gap_count + 1;
            end
            if (r_done_sig) r_done = r_done + 1;
            if (w_busy) w_cycles = w_cycles + 1;
            if (w_fifo_valid && w_fifo_ready) w_fill = w_fill + 1;
            if (w_awvalid && w_awready) w_aw = w_aw + 1;
            if (w_wvalid && w_wready) w_w = w_w + 1;
            if (w_bvalid && w_bready) w_b = w_b + 1;
            if (w_done_sig) w_done = w_done + 1;
        end
    end

    task reset_counters;
        begin
            r_cycles=0; r_beats=0; r_pixels=0; r_ar=0; r_yield=0; r_done=0;
            w_cycles=0; w_fill=0; w_aw=0; w_w=0; w_b=0; w_done=0;
            r_first_latency_sum=0; r_first_latency_max=0; r_gap_max=0;
            r_stall_cycles=0; r_yield_to_ar_sum=0; r_yield_to_ar_max=0;
            r_ar_cycle=0; r_first_ar_cycle=0; r_last_beat_cycle=0; r_yield_cycle=0;
            r_first_pending=0; r_yield_pending=0; r_ar_count=0;
            r_gap_count=0; r_yield_ar_count=0;
        end
    endtask

    task run_reader;
        input integer lat;
        integer guard;
        begin
            read_latency = lat; r_fifo_full = 1'b0; r_cmd_length = 32'd64;
            r_cmd_stride = 32'd64; r_cmd_rows = 7'd1;
            r_cmd_valid = 1'b0; @(posedge clk); r_cmd_valid = 1'b1;
            while (!r_cmd_ready) @(posedge clk);
            @(posedge clk); r_cmd_valid = 1'b0;
            guard=0; while (!r_done_sig && guard < 5000) begin @(negedge clk); guard=guard+1; end
            $display("READER bytes=64 initial_delay=%0d cycles=%0d beats=%0d AR=%0d yield=%0d done=%0d error=%b", lat, r_cycles, r_beats, r_ar, r_yield, r_done, r_error);
            if (!r_done_sig || r_error) begin
                $display("ERROR: single-row reader completion failed done=%0d pulse=%b error=%b", r_done, r_done_sig, r_error);
                failures = failures + 1;
            end
        end
    endtask

    task run_reader_metrics;
        input integer lat;
        input integer row_bytes_arg;
        input integer rows_arg;
        integer guard;
        begin
            read_latency = lat; r_fifo_full = 1'b0; r_cmd_length = row_bytes_arg;
            r_cmd_stride = row_bytes_arg; r_cmd_rows = rows_arg;
            r_cmd_valid = 1'b0; @(posedge clk); r_cmd_valid = 1'b1;
            while (!r_cmd_ready) @(posedge clk);
            @(posedge clk); r_cmd_valid = 1'b0;
            guard=0; while (!r_done_sig && guard < 20000) begin @(negedge clk); guard=guard+1; end
            if (!r_done_sig)
                $display("DEBUG reader state=%0d rows_left=%0d active_beats=%0d cmd_last=%b desc_count=%0d rd=%0d wr=%0d candidate=%b",
                    u_reader.state, u_reader.issue_rows_left,
                    u_reader.active_beats_left, u_reader.active_cmd_last,
                    u_reader.desc_count, u_reader.desc_rd_ptr,
                    u_reader.desc_wr_ptr, u_reader.candidate_valid);
            if (!r_done_sig)
                $display("DEBUG responder active=%b rvalid=%b rready=%b pending_beats=%0d qcount=%0d qrd=%0d qwr=%0d",
                    r_active, r_rvalid, r_rready, r_pending_beats,
                    r_queue_count, r_queue_rd, r_queue_wr);
            $display("METRICS row_bytes=%0d rows=%0d Rdelay=%0d cycles=%0d beats=%0d pixels=%0d AR=%0d done=%0d error=%b first_AR_to_firstR=%0d R_gap_max=%0d RVALID_without_RREADY=%0d owner_yield_to_next_AR=%0d bursts=%0d", r_cmd_length, r_cmd_rows, lat,
                r_cycles,
                r_beats, r_pixels,
                r_ar_count, r_done, r_error,
                r_first_latency_max,
                r_gap_max, r_stall_cycles,
                (r_yield_ar_count != 0) ? r_yield_to_ar_max : -1,
                r_ar_count);
        end
    endtask

    task run_writer;
        input integer lat;
        integer guard;
        begin
            b_delay = lat; w_bus_grant = 1'b1; w_awready = 1'b1; w_wready = 1'b1;
            w_fifo_valid = 1'b1; w_fifo_data = 16'h1234;
            w_cmd_valid = 1'b0; @(posedge clk); w_cmd_valid = 1'b1;
            while (!w_cmd_ready) @(posedge clk);
            @(posedge clk); w_cmd_valid = 1'b0;
            guard=0; while (!w_done_sig && guard < 5000) begin @(posedge clk); guard=guard+1; end
            @(posedge clk);
            $display("WRITER Bdelay=%0d cycles=%0d fill=%0d AW=%0d W=%0d B=%0d done=%0d error=%b", lat, w_cycles, w_fill, w_aw, w_w, w_b, w_done, w_error);
            w_fifo_valid = 1'b0;
        end
    endtask

    initial begin
        failures=0; rst_n=1'b0; r_cmd_valid=0; r_fifo_full=0; read_latency=0;
        r_cmd_length=0; r_cmd_stride=0; r_cmd_rows=0;
        w_cmd_valid=0; w_fifo_valid=0; w_fifo_data=16'h1234; w_bus_grant=0;
        w_awready=0; w_wready=0; w_bid=0; w_bresp=0; w_bvalid=0; b_delay=0;
        reset_counters();
        repeat (5) @(posedge clk); rst_n=1'b1; repeat (2) @(posedge clk);
        reset_counters(); run_reader(0); reset_counters(); run_writer(0);
        reset_counters(); run_reader(2); reset_counters(); run_writer(2);
        reset_counters(); run_reader(4); reset_counters(); run_writer(4);
        reset_counters(); run_reader(32); reset_counters(); run_reader(64);
        reset_counters(); run_reader(128); reset_counters(); run_reader(192);
        reset_counters(); run_reader_metrics(0,64,32);
        if (r_ar_count != 32 || r_beats != 512 || r_pixels != 1024 || r_done != 1 || r_error) begin
            $display("ERROR: 32-row batch validation failed AR=%0d beats=%0d pixels=%0d done=%0d error=%b", r_ar_count, r_beats, r_pixels, r_done, r_error);
            failures = failures + 1;
        end
        reset_counters(); run_reader_metrics(32,32,16);
        if (r_ar_count != 16 || r_beats != 128 || r_pixels != 256 || r_done != 1 || r_error) begin
            $display("ERROR: narrow-row batch validation failed AR=%0d beats=%0d pixels=%0d done=%0d error=%b", r_ar_count, r_beats, r_pixels, r_done, r_error);
            failures = failures + 1;
        end
        reset_counters(); run_reader_metrics(0,48,24);
        if (r_ar_count != 24 || r_beats != 288 || r_pixels != 576 || r_done != 1 || r_error) begin
            $display("ERROR: 24x24 batch validation failed AR=%0d beats=%0d pixels=%0d done=%0d error=%b", r_ar_count, r_beats, r_pixels, r_done, r_error);
            failures = failures + 1;
        end
        reset_counters(); run_reader_metrics(0,2560,1);
        if (r_ar_count != 40 || r_beats != 640 || r_pixels != 1280 || r_done != 1 || r_error) begin
            $display("ERROR: multi-burst row validation failed AR=%0d beats=%0d pixels=%0d done=%0d error=%b", r_ar_count, r_beats, r_pixels, r_done, r_error);
            failures = failures + 1;
        end
        if (failures != 0) $fatal(1, "reader/writer regression failures=%0d", failures);
        $finish;
    end
endmodule
