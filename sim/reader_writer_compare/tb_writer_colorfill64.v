`timescale 1ns/1ps

module tb_writer_colorfill64;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst_n = 1'b0;
    reg cmd_valid = 1'b0;
    wire cmd_ready;
    reg [31:0] cmd_addr = 0;
    reg [31:0] cmd_length_bytes = 0;
    reg [15:0] cmd_color = 0;
    wire bus_request;
    reg bus_grant = 1'b1;
    wire burst_done, busy, done, error;
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
    reg [3:0] bid = 0;
    reg [1:0] bresp = 0;
    reg bvalid = 0;
    wire bready;

    reg [7:0] memory [0:65535];
    reg written [0:65535];
    integer base_addr;
    integer beat_index;
    integer aw_count, w_count, b_count;
    integer cycle_count, first_bus_request_cycle;
    integer failures, lane, j, guard;
    integer test_addr, test_length, test_cycles;
    reg [15:0] test_color;

    axi_rgb565_writer #(.BURST_LEN(8)) dut (
        .clk(clk), .rst_n(rst_n), .cmd_valid(cmd_valid), .cmd_ready(cmd_ready),
        .cmd_addr(cmd_addr), .cmd_length_bytes(cmd_length_bytes),
        .cmd_constant_fill(1'b1), .cmd_constant_data(cmd_color),
        .bus_request(bus_request), .bus_grant(bus_grant),
        .burst_done(burst_done), .busy(busy), .done(done), .error(error),
        .fifo_valid(1'b0), .fifo_ready(fifo_ready), .fifo_data(16'd0),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst), .m_axi_awcache(awcache),
        .m_axi_awqos(awqos), .m_axi_awregion(awregion), .m_axi_awprot(awprot),
        .m_axi_awlock(awlock), .m_axi_awvalid(awvalid), .m_axi_awready(awready),
        .m_axi_wdata(wdata), .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready), .m_axi_bid(bid),
        .m_axi_bresp(bresp), .m_axi_bvalid(bvalid), .m_axi_bready(bready)
    );

    always @(posedge clk) begin
        if (!rst_n) begin
            cycle_count <= 0;
            first_bus_request_cycle <= -1;
            bvalid <= 1'b0;
            beat_index <= 0;
            base_addr <= 0;
            aw_count <= 0;
            w_count <= 0;
            b_count <= 0;
        end else begin
            cycle_count <= cycle_count + 1;
            if (bus_request && first_bus_request_cycle == -1)
                first_bus_request_cycle <= cycle_count;

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
                            wdata[lane * 8 +: 8];
                        written[base_addr + beat_index * 8 + lane] <= 1'b1;
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
        input [15:0] color_arg;
        integer k;
        integer expected_byte;
        begin
            @(negedge clk);
            rst_n = 1'b0;
            cmd_valid = 1'b0;
            cmd_addr = addr_arg;
            cmd_length_bytes = length_arg;
            cmd_color = color_arg;
            for (k = 0; k < 65536; k = k + 1) begin
                memory[k] = 0;
                written[k] = 1'b0;
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
                $display("FAIL timeout addr=%08x length=%0d", addr_arg, length_arg);
                failures = failures + 1;
            end
            if (error) begin
                $display("FAIL writer error addr=%08x length=%0d", addr_arg, length_arg);
                failures = failures + 1;
            end
            if (fifo_ready) begin
                $display("FAIL constant mode requested FIFO data");
                failures = failures + 1;
            end

            for (k = 0; k < length_arg; k = k + 1) begin
                expected_byte = (k & 1) ? ((color_arg >> 8) & 255) :
                                         (color_arg & 255);
                if (!written[addr_arg + k] || memory[addr_arg + k] != expected_byte) begin
                    $display("FAIL byte addr=%08x len=%0d offset=%0d got=%02x valid=%0d expected=%02x",
                             addr_arg, length_arg, k, memory[addr_arg + k],
                             written[addr_arg + k], expected_byte);
                    failures = failures + 1;
                end
            end

            test_cycles = first_bus_request_cycle;
            if (test_cycles > 12) begin
                $display("FAIL slow fill addr=%08x first request after %0d cycles",
                         addr_arg, test_cycles);
                failures = failures + 1;
            end
            $display("CASE addr=%08x len=%0d first_request=%0d cycles bursts=%0d beats=%0d",
                     addr_arg, length_arg, test_cycles, aw_count, w_count);
        end
    endtask

    initial begin
        failures = 0;
        run_case(32'h0000_2000, 64, 16'hA53C);
        run_case(32'h0000_2002, 64, 16'hA53C);
        run_case(32'h0000_2006, 64, 16'hA53C);
        run_case(32'h0000_2FFA, 64, 16'hA53C);

        if (failures == 0)
            $display("PASS: 64-bit colorfill expansion, byte strobes, and burst boundaries");
        else
            $display("FAILURES: %0d", failures);
        $finish;
    end
endmodule
