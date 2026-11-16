`timescale 1ns/1ps

module tb_writer_pixel_stream;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst_n;
    reg cmd_valid;
    wire cmd_ready;
    reg [31:0] cmd_addr;
    reg [31:0] cmd_length_bytes;
    wire bus_request;
    wire burst_done;
    wire busy;
    wire done;
    wire error;
    reg fifo_valid;
    wire fifo_ready;
    reg [15:0] fifo_data;
    integer fifo_index;
    reg bus_grant;

    wire [3:0] awid;
    wire [31:0] awaddr;
    wire [7:0] awlen;
    wire [2:0] awsize;
    wire [1:0] awburst;
    wire [3:0] awcache, awqos, awregion;
    wire [2:0] awprot;
    wire awlock, awvalid;
    reg awready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    wire wlast, wvalid;
    reg wready;
    reg [3:0] bid;
    reg [1:0] bresp;
    reg bvalid;
    wire bready;

    integer memory [0:16383];
    integer written [0:16383];
    integer base_addr;
    integer beat_index;
    integer aw_count;
    integer w_count;
    integer b_count;
    integer first_request_pixels;
    integer fifo_run;
    integer fifo_run_max;
    integer failures;
    integer i, guard;
    integer lane;
    integer test_addr;
    integer test_length;
    integer case_index;

    axi_rgb565_writer #(.BURST_LEN(16)) dut (
        .clk(clk), .rst_n(rst_n), .cmd_valid(cmd_valid), .cmd_ready(cmd_ready),
        .cmd_addr(cmd_addr), .cmd_length_bytes(cmd_length_bytes),
        .cmd_constant_fill(1'b0), .cmd_constant_data(16'd0),
        .bus_request(bus_request), .bus_grant(bus_grant),
        .burst_done(burst_done), .busy(busy), .done(done), .error(error),
        .fifo_valid(fifo_valid), .fifo_ready(fifo_ready), .fifo_data(fifo_data),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst), .m_axi_awcache(awcache),
        .m_axi_awqos(awqos), .m_axi_awregion(awregion), .m_axi_awprot(awprot),
        .m_axi_awlock(awlock), .m_axi_awvalid(awvalid), .m_axi_awready(awready),
        .m_axi_wdata(wdata), .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready), .m_axi_bid(bid),
        .m_axi_bresp(bresp), .m_axi_bvalid(bvalid), .m_axi_bready(bready)
    );

    always @* fifo_data = 16'h4000 + fifo_index;

    always @(posedge clk) begin
        if (!rst_n) begin
            fifo_index <= 0;
            bvalid <= 1'b0;
            beat_index <= 0;
            base_addr <= 0;
            aw_count <= 0;
            w_count <= 0;
            b_count <= 0;
            first_request_pixels <= -1;
            fifo_run <= 0;
            fifo_run_max <= 0;
        end else begin
            if (fifo_valid && fifo_ready) begin
                fifo_index <= fifo_index + 1;
                fifo_run <= fifo_run + 1;
                if ((fifo_run + 1) > fifo_run_max)
                    fifo_run_max <= fifo_run + 1;
            end else begin
                fifo_run <= 0;
            end

            if (bus_request && first_request_pixels == -1)
                first_request_pixels <= fifo_index;

            bvalid <= 1'b0;
            if (awvalid && awready) begin
                base_addr <= awaddr;
                beat_index <= 0;
                aw_count <= aw_count + 1;
            end
            if (wvalid && wready) begin
                for (lane = 0; lane < 8; lane = lane + 1) begin
                    if (wstrb[lane]) begin
                        memory[base_addr + beat_index * 8 + lane] <=
                            (wdata >> (lane * 8)) & 255;
                        written[base_addr + beat_index * 8 + lane] <= 1;
                    end
                end
                w_count <= w_count + 1;
                beat_index <= beat_index + 1;
                if (wlast)
                    bvalid <= 1'b1;
            end
            if (bvalid && bready)
                b_count <= b_count + 1;
        end
    end

    task run_case;
        input integer addr_arg;
        input integer length_arg;
        integer j;
        integer pixel_value;
        integer expected_byte;
        begin
            @(negedge clk);
            rst_n = 1'b0;
            cmd_valid = 1'b0;
            cmd_addr = addr_arg;
            cmd_length_bytes = length_arg;
            fifo_valid = 1'b1;
            bus_grant = 1'b1;
            awready = 1'b1;
            wready = 1'b1;
            bid = 0;
            bresp = 0;
            for (j = 0; j < 16384; j = j + 1) begin
                memory[j] = 0;
                written[j] = 0;
            end
            repeat (2) @(negedge clk);
            rst_n = 1'b1;
            @(negedge clk);
            cmd_valid = 1'b1;
            while (!cmd_ready) @(negedge clk);
            @(negedge clk);
            cmd_valid = 1'b0;

            guard = 0;
            while (!done && guard < 1000) begin
                @(negedge clk);
                guard = guard + 1;
            end
            if (!done) begin
                $display("FAIL timeout addr=%0d length=%0d", addr_arg, length_arg);
                failures = failures + 1;
            end
            if (error) begin
                $display("FAIL writer error addr=%0d length=%0d", addr_arg, length_arg);
                failures = failures + 1;
            end

            for (j = 0; j < length_arg; j = j + 1) begin
                pixel_value = 16'h4000 + (j / 2);
                if ((j % 2) == 0)
                    expected_byte = pixel_value & 255;
                else
                    expected_byte = (pixel_value >> 8) & 255;
                if (!written[addr_arg + j] ||
                    memory[addr_arg + j] != expected_byte) begin
                    $display("FAIL byte addr=%0d len=%0d offset=%0d got=%02x valid=%0d expected=%02x",
                             addr_arg, length_arg, j,
                             memory[addr_arg + j], written[addr_arg + j], expected_byte);
                    failures = failures + 1;
                end
            end
            if (fifo_index != ((length_arg + 1) / 2)) begin
                $display("FAIL FIFO pixels addr=%0d len=%0d consumed=%0d expected=%0d",
                         addr_arg, length_arg, fifo_index, (length_arg + 1) / 2);
                failures = failures + 1;
            end
            if (length_arg == 64 && (addr_arg % 4) == 0 &&
                (first_request_pixels != 32 || fifo_run_max != 32)) begin
                $display("FAIL burst staging addr=%08x first_request_pixels=%0d max_contiguous_accepts=%0d",
                         addr_arg, first_request_pixels, fifo_run_max);
                failures = failures + 1;
            end
            $display("CASE addr=%08x length=%0d pixels=%0d bursts=%0d beats=%0d",
                     addr_arg, length_arg, fifo_index, aw_count, w_count);
        end
    endtask

    initial begin
        failures = 0;
        rst_n = 0;
        cmd_valid = 0;
        cmd_addr = 0;
        cmd_length_bytes = 0;
        fifo_valid = 1;
        bus_grant = 1;
        awready = 1;
        wready = 1;
        bid = 0;
        bresp = 0;
        bvalid = 0;
        first_request_pixels = -1;
        fifo_run = 0;
        fifo_run_max = 0;

        run_case(32'h0000_2000, 1);
        run_case(32'h0000_2001, 3);
        run_case(32'h0000_2002, 62);
        run_case(32'h0000_2003, 63);
        run_case(32'h0000_2000, 64);
        run_case(32'h0000_2001, 65);
        run_case(32'h0000_2002, 127);
        run_case(32'h0000_2003, 129);
        run_case(32'h0000_2fff, 65);

        if (failures == 0)
            $display("PASS: writer byte stream and boundary cases");
        else
            $display("FAILURES: %0d", failures);
        $finish;
    end
endmodule
