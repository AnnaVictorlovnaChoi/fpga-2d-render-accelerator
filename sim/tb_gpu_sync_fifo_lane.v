`timescale 1ns/1ps
module tb_gpu_sync_fifo_lane;
    reg clk = 0;
    reg rst_n = 0;
    reg wr_en = 0;
    reg [31:0] wr_data = 0;
    reg [1:0] wr_keep = 0;
    reg rd_en = 0;
    wire [15:0] rd_data;
    wire full, prog_full, empty;
    wire [7:0] count_used;

    gpu_sync_fifo #(.WR_WIDTH(32), .RD_WIDTH(16), .DEPTH(8), .PROG_FULL_LEVEL(7)) dut (
        .clk(clk), .rst_n(rst_n), .wr_en(wr_en), .wr_data(wr_data), .wr_keep(wr_keep),
        .rd_en(rd_en), .rd_data(rd_data), .full(full), .prog_full(prog_full),
        .empty(empty), .count_used(count_used));

    always #5 clk = ~clk;

    task write_word;
        input [31:0] data;
        input [1:0] keep;
        begin
            @(negedge clk); wr_data = data; wr_keep = keep; wr_en = 1'b1;
            @(negedge clk); wr_en = 1'b0;
        end
    endtask

    initial begin
        repeat (2) @(negedge clk);
        rst_n = 1'b1;
        // low-only, upper-only, then both lanes
        write_word(32'hbbbb_aaaa, 2'b01);
        write_word(32'hdddd_cccc, 2'b10);
        write_word(32'hffff_eeee, 2'b11);
        @(negedge clk); rd_en = 1'b1;
        #1;
        if (rd_data !== 16'haaaa) $fatal(1, "low lane mismatch: %h", rd_data);
        @(posedge clk); #1;
        if (rd_data !== 16'hdddd) $fatal(1, "upper-only lane mismatch: %h", rd_data);
        @(posedge clk); #1;
        if (rd_data !== 16'heeee) $fatal(1, "double lane low mismatch: %h", rd_data);
        @(posedge clk); #1;
        if (rd_data !== 16'hffff) $fatal(1, "double lane high mismatch: %h", rd_data);
        @(posedge clk); #1;
        rd_en = 1'b0;
        if (!empty) begin
            $display("FAIL: FIFO did not consume all valid lanes");
            $fatal;
        end
        $display("PASS: FIFO lane sequence");
        $finish;
    end
endmodule
