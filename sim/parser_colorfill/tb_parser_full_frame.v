`timescale 1ns/1ps

module tb_parser_full_frame;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst_n = 1'b0;
    reg cmd_valid = 1'b0;
    reg [31:0] ctrl = 32'h0000_0101;
    reg [31:0] src = 0;
    reg [31:0] dst = 32'h0600_0000;
    reg [31:0] wh = {16'd720, 16'd1280};
    reg [31:0] src_stride = 0;
    reg [31:0] dst_stride = 32'd2560;
    reg [31:0] color = 32'h0000_f800;
    wire [2:0] op;
    wire [15:0] color_out;
    wire [7:0] scale_q26;
    wire [31:0] scale_src_base, scale_src_stride;
    wire read_cmd_valid, dst_read_cmd_valid, write_cmd_valid;
    wire [31:0] read_cmd_addr, read_cmd_length_bytes, read_cmd_stride_bytes;
    wire [6:0] read_cmd_rows;
    wire [31:0] dst_read_cmd_addr, dst_read_cmd_length_bytes, dst_read_cmd_stride_bytes;
    wire [6:0] dst_read_cmd_rows;
    wire [31:0] write_cmd_addr, write_cmd_length_bytes, write_cmd_stride_bytes;
    wire [6:0] write_cmd_rows;
    reg write_done = 1'b0;
    integer done_delay = 0;
    integer row_count = 0;
    integer errors = 0;
    wire bitblt_busy, bitblt_done, bitblt_error;

    bitblt_cmd_parser dut (
        .clk(clk), .rst_n(rst_n), .ctrl(ctrl), .src(src), .dst(dst), .wh(wh),
        .src_stride(src_stride), .dst_stride(dst_stride), .color(color),
        .cmd_valid(cmd_valid), .op(op), .color_out(color_out), .scale_q26(scale_q26),
        .scale_src_base(scale_src_base), .scale_src_stride(scale_src_stride),
        .read_cmd_valid(read_cmd_valid), .read_cmd_ready(1'b1),
        .read_cmd_addr(read_cmd_addr), .read_cmd_length_bytes(read_cmd_length_bytes),
        .read_cmd_stride_bytes(read_cmd_stride_bytes), .read_cmd_rows(read_cmd_rows),
        .read_busy(1'b0), .read_done(1'b0), .read_error(1'b0),
        .dst_read_cmd_valid(dst_read_cmd_valid), .dst_read_cmd_ready(1'b1),
        .dst_read_cmd_addr(dst_read_cmd_addr),
        .dst_read_cmd_length_bytes(dst_read_cmd_length_bytes),
        .dst_read_cmd_stride_bytes(dst_read_cmd_stride_bytes), .dst_read_cmd_rows(dst_read_cmd_rows),
        .dst_read_busy(1'b0), .dst_read_done(1'b0), .dst_read_error(1'b0),
        .write_cmd_valid(write_cmd_valid), .write_cmd_ready(1'b1),
        .write_cmd_addr(write_cmd_addr), .write_cmd_length_bytes(write_cmd_length_bytes),
        .write_cmd_stride_bytes(write_cmd_stride_bytes), .write_cmd_rows(write_cmd_rows),
        .write_busy(1'b0), .write_done(write_done), .write_error(1'b0),
        .bitblt_busy(bitblt_busy), .bitblt_done(bitblt_done), .bitblt_error(bitblt_error)
    );

    always @(posedge clk) begin
        if (!rst_n) begin
            write_done <= 1'b0;
            done_delay <= 0;
            row_count <= 0;
            errors <= 0;
        end else begin
            write_done <= 1'b0;
            if (write_cmd_valid) begin
                if (write_cmd_addr !== (32'h0600_0000 + row_count * 32'd2560) ||
                    write_cmd_length_bytes !== 32'd2560 || write_cmd_rows !== 7'd1) begin
                    $display("BAD row=%0d addr=%08x len=%0d rows=%0d",
                             row_count, write_cmd_addr, write_cmd_length_bytes, write_cmd_rows);
                    errors <= errors + 1;
                end
                row_count <= row_count + 1;
                done_delay <= 2;
            end else if (done_delay > 0) begin
                done_delay <= done_delay - 1;
                if (done_delay == 1)
                    write_done <= 1'b1;
            end
        end
    end

    initial begin
        repeat (4) @(negedge clk);
        rst_n = 1'b1;
        @(negedge clk);
        cmd_valid = 1'b1;
        @(negedge clk);
        cmd_valid = 1'b0;
        wait (bitblt_done);
        @(negedge clk);
        $display("RESULT rows=%0d errors=%0d busy=%0b", row_count, errors, bitblt_busy);
        if (row_count != 720 || errors != 0 || bitblt_error || bitblt_busy)
            $fatal(1, "full-frame colorfill parser regression");
        $display("PASS: 720 rows, 2560 bytes each, addresses contiguous by 2560 bytes");
        $finish;
    end

    initial begin
        #1000000;
        $fatal(1, "timeout");
    end
endmodule
