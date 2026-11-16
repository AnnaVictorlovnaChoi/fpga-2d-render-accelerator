`timescale 1ns/1ps
module tb_axi_gpu_perf_regs;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst_n = 0;
    reg [7:0] awid = 0, arid = 0;
    reg [31:0] awaddr = 0, wdata = 0, araddr = 0;
    reg [7:0] awlen = 0, arlen = 0;
    reg [2:0] awsize = 2, arsize = 2, awprot = 0, arprot = 0;
    reg [1:0] awburst = 1, arburst = 1;
    reg [3:0] awcache = 0, awqos = 0, awregion = 0;
    reg [3:0] arcache = 0, arqos = 0, arregion = 0;
    reg awlock = 0, awvalid = 0, wlast = 1, wvalid = 0;
    reg [3:0] wstrb = 15;
    reg bready = 0, arlock = 0, arvalid = 0, rready = 0;
    wire awready, wready, bvalid, arready, rvalid, rlast;
    wire [7:0] bid, rid;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata, cmd_data;
    wire [3:0] cmd_strb;
    wire cmd_wr_en, perf_clear;
    reg cmd_full = 0, cmd_prog_full = 0, cmd_empty = 1;
    reg engine_busy = 0, engine_done = 0, engine_error = 0;
    reg [31:0] perf_ar_count = 32'h12345678;

    axi_gpu_cmd_axi_slave dut (
        .s_axi_aclk(clk), .s_axi_aresetn(rst_n),
        .s_axi_awid(awid), .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awsize(awsize), .s_axi_awburst(awburst),
        .s_axi_awcache(awcache), .s_axi_awqos(awqos),
        .s_axi_awregion(awregion), .s_axi_awprot(awprot),
        .s_axi_awlock(awlock), .s_axi_awvalid(awvalid),
        .s_axi_awready(awready), .s_axi_wdata(wdata), .s_axi_wstrb(wstrb),
        .s_axi_wlast(wlast), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bid(bid), .s_axi_bresp(bresp), .s_axi_bvalid(bvalid),
        .s_axi_bready(bready), .s_axi_arid(arid), .s_axi_araddr(araddr),
        .s_axi_arlen(arlen), .s_axi_arsize(arsize), .s_axi_arburst(arburst),
        .s_axi_arcache(arcache), .s_axi_arqos(arqos),
        .s_axi_arregion(arregion), .s_axi_arprot(arprot),
        .s_axi_arlock(arlock), .s_axi_arvalid(arvalid),
        .s_axi_arready(arready), .s_axi_rid(rid), .s_axi_rdata(rdata),
        .s_axi_rresp(rresp), .s_axi_rlast(rlast), .s_axi_rvalid(rvalid),
        .s_axi_rready(rready), .cmd_fifo_wr_en(cmd_wr_en),
        .cmd_fifo_wr_data(cmd_data), .cmd_fifo_wr_strb(cmd_strb),
        .cmd_fifo_full(cmd_full), .cmd_fifo_prog_full(cmd_prog_full),
        .cmd_fifo_empty(cmd_empty), .engine_busy(engine_busy),
        .engine_done(engine_done), .engine_error(engine_error),
        .perf_clear(perf_clear), .perf_ar_count(perf_ar_count),
        .perf_r_count(32'd2), .perf_aw_count(32'd3), .perf_w_count(32'd4),
        .perf_b_count(32'd5), .perf_ar_first_r_sum(32'd6),
        .perf_ar_first_r_max(32'd7), .perf_r_stall_cycles(32'd8),
        .perf_r_gap_max(32'd9), .perf_read_owner_cycles(32'd10),
        .perf_write_owner_cycles(32'd11), .perf_read_wait_cycles(32'd12),
        .perf_write_wait_cycles(32'd13), .perf_cmd_count(32'd14),
        .perf_done_count(32'd15), .perf_parser_read_wait_cycles(32'd16),
        .perf_parser_write_wait_cycles(32'd17), .perf_elapsed_cycles(32'd18),
        .perf_read_no_r_cycles(32'd19), .perf_read_contended_cycles(32'd20),
        .perf_write_contended_cycles(32'd21), .perf_aw_stall_cycles(32'd22),
        .perf_w_stall_cycles(32'd23), .perf_aw_to_first_w_sum(32'd24),
        .perf_aw_to_first_w_max(32'd25), .perf_w_gap_max(32'd26),
        .perf_w_to_b_sum(32'd27), .perf_b_wait_cycles(32'd28),
        .perf_read_gap_active_cycles(32'd29), .perf_read_gap_active_max(32'd30),
        .perf_read_release_to_ar_sum(32'd31), .perf_read_release_to_ar_max(32'd32),
        .perf_read_release_to_ar_samples(32'd33),
        .perf_ar_first_r_samples(32'd34), .perf_ar_first_r_orphan(32'd35)
    );

    initial begin
        repeat (2) @(posedge clk);
        rst_n = 1;

        @(negedge clk);
        araddr = 32'h0c;
        arvalid = 1;
        while (!arready) @(negedge clk);
        @(posedge clk);
        #1;
        arvalid = 0;
        if (!rvalid || rdata !== 32'h12345678)
            $fatal(1, "debug counter read mismatch: valid=%b data=%h", rvalid, rdata);
        rready = 1;
        @(posedge clk);
        #1 rready = 0;

        @(negedge clk);
        araddr = 32'h94;
        arvalid = 1;
        while (!arready) @(negedge clk);
        @(posedge clk);
        #1;
        arvalid = 0;
        if (!rvalid || rdata !== 32'd35)
            $fatal(1, "new timing counter read mismatch: valid=%b data=%h", rvalid, rdata);
        rready = 1;
        @(posedge clk);
        #1 rready = 0;

        @(negedge clk);
        awaddr = 32'h08;
        wdata = 1;
        awvalid = 1;
        wvalid = 1;
        while (!(awready && wready)) @(negedge clk);
        @(posedge clk);
        #1;
        awvalid = 0;
        wvalid = 0;
        @(negedge clk);
        if (!perf_clear || cmd_wr_en)
            $fatal(1, "debug clear reached wrong destination: clear=%b cmd_wr=%b",
                   perf_clear, cmd_wr_en);

        $display("PASS: debug counter reads and clear write are isolated from command FIFO");
        $finish;
    end
endmodule
