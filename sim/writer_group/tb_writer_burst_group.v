`timescale 1ns/1ps

module tb_writer_burst_group;
    reg clk = 1'b0;
    always #5 clk = ~clk;

    reg rst_n = 1'b0;
    reg cmd_valid = 1'b0;
    wire cmd_ready;
    reg [31:0] cmd_addr = 32'd0;
    reg [31:0] cmd_length_bytes = 32'd64;
    reg cmd_last = 1'b0;
    wire bus_request;
    wire bus_release;
    wire burst_done;
    wire busy;
    wire done;
    wire error;
    wire fifo_ready;
    wire [3:0] awid;
    wire [31:0] awaddr;
    wire [7:0] awlen;
    wire [2:0] awsize;
    wire [1:0] awburst;
    wire [3:0] awcache, awqos, awregion;
    wire [2:0] awprot;
    wire awlock, awvalid;
    reg awready = 1'b1;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    wire wlast, wvalid;
    reg wready = 1'b1;
    reg [3:0] bid = 4'd0;
    reg [1:0] bresp = 2'b00;
    reg bvalid = 1'b0;
    wire bready;

    integer release_count = 0;
    integer aw_count = 0;
    integer w_count = 0;
    reg [7:0] strb_log [0:63];

    axi_rgb565_writer #(.AXI_ADDR_WIDTH(32), .AXI_ID_WIDTH(4), .BURST_LEN(8)) dut (
        .clk(clk), .rst_n(rst_n),
        .cmd_valid(cmd_valid), .cmd_ready(cmd_ready), .cmd_addr(cmd_addr),
        .cmd_length_bytes(cmd_length_bytes), .cmd_last(cmd_last),
        .cmd_constant_fill(1'b1), .cmd_constant_data(16'hA55A),
        .bus_request(bus_request), .bus_release(bus_release), .bus_abort(1'b0), .bus_grant(1'b1),
        .burst_done(burst_done), .busy(busy), .done(done), .error(error),
        .fifo_valid(1'b0), .fifo_ready(fifo_ready), .fifo_data(16'd0),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst), .m_axi_awcache(awcache),
        .m_axi_awqos(awqos), .m_axi_awregion(awregion), .m_axi_awprot(awprot),
        .m_axi_awlock(awlock), .m_axi_awvalid(awvalid), .m_axi_awready(awready),
        .m_axi_wdata(wdata), .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bid(bid), .m_axi_bresp(bresp), .m_axi_bvalid(bvalid),
        .m_axi_bready(bready)
    );

    always @(posedge clk) begin
        if (!rst_n) begin
            bvalid <= 1'b0;
            release_count <= 0;
            aw_count <= 0;
            w_count <= 0;
        end else begin
            if (wvalid && wready && wlast)
                bvalid <= 1'b1;
            else if (bvalid && bready)
                bvalid <= 1'b0;

            if (bus_release)
                release_count <= release_count + 1;
            if (awvalid && awready)
                aw_count <= aw_count + 1;
            if (wvalid && wready) begin
                strb_log[w_count] <= wstrb;
                w_count <= w_count + 1;
            end
        end
    end

    task send_row;
        input last_row;
        input [31:0] address;
        input [31:0] length;
        begin
            @(negedge clk);
            while (!cmd_ready) @(negedge clk);
            cmd_addr = address;
            cmd_length_bytes = length;
            cmd_last = last_row;
            cmd_valid = 1'b1;
            @(negedge clk);
            cmd_valid = 1'b0;
            wait (done);
            repeat (2) @(posedge clk);
            @(negedge clk);
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst_n = 1'b1;
        send_row(1'b0, 32'h0000_1000, 32'd64);
        if (release_count != 1) $fatal(1, "first burst did not release owner");
        send_row(1'b0, 32'h0000_1040, 32'd64);
        if (release_count != 2) $fatal(1, "second burst did not release owner");
        send_row(1'b0, 32'h0000_1080, 32'd64);
        if (release_count != 3) $fatal(1, "third burst did not release owner");
        send_row(1'b1, 32'h0000_10C0, 32'd64);
        if (release_count != 4) $fatal(1, "fourth burst did not release owner");

        send_row(1'b0, 32'h0000_1100, 32'd64);
        if (release_count != 5) $fatal(1, "fifth burst did not release owner");
        send_row(1'b1, 32'h0000_1140, 32'd64);
        if (release_count != 6) $fatal(1, "sixth burst did not release owner");
        send_row(1'b1, 32'h0000_2006, 32'd8);
        if (strb_log[48] != 8'hC0 || strb_log[49] != 8'h3F)
            $fatal(1, "two-beat edge strobes incorrect: %02h %02h", strb_log[48], strb_log[49]);
        send_row(1'b1, 32'h0000_3002, 32'd6);
        if (strb_log[50] != 8'hFC)
            $fatal(1, "single-beat edge strobe incorrect: %02h", strb_log[50]);
        if (aw_count != 8 || w_count != 51)
            $fatal(1, "unexpected AXI transfer count aw=%0d w=%0d", aw_count, w_count);

        if (release_count != 8)
            $fatal(1, "each AXI burst must release owner, releases=%0d", release_count);
        $display("PASS: owner released after every burst");
        $finish;
    end
endmodule
