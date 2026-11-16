`timescale 1ns/1ps

module tb_parser_scale_addr;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst_n = 1'b0;
    reg cmd_valid = 1'b0;
    reg [31:0] ctrl = 32'h0000_0401;
    reg [31:0] src = 32'h0010_0000;
    reg [31:0] dst = 32'h0020_0000;
    reg [31:0] wh = {16'd3, 16'd4};
    reg [31:0] src_stride = 32'h100;
    reg [31:0] dst_stride = 32'd8;
    reg [31:0] color = 32'd96;
    wire [2:0] op;
    wire [15:0] color_out;
    wire [3:0] cache_control;
    wire [15:0] cache_row;
    wire cache_last_batch;
    wire [7:0] scale_q26;
    wire [31:0] scale_src_base, scale_src_stride;
    wire read_cmd_valid;
    reg read_done = 1'b0;
    wire [31:0] read_cmd_addr, read_cmd_length_bytes, read_cmd_stride_bytes;
    wire [6:0] read_cmd_rows;
    wire dst_read_cmd_valid;
    wire [31:0] dst_read_cmd_addr, dst_read_cmd_length_bytes, dst_read_cmd_stride_bytes;
    wire [6:0] dst_read_cmd_rows;
    wire write_cmd_valid;
    wire [31:0] write_cmd_addr, write_cmd_length_bytes;
    wire write_cmd_last;
    reg write_done = 1'b0;
    wire bitblt_busy, bitblt_done, bitblt_error;
    integer read_count = 0;
    integer write_delay = 0;
    integer errors = 0;

    bitblt_cmd_parser #(.ADDR_WIDTH(32)) dut (
        .clk(clk), .rst_n(rst_n), .ctrl(ctrl), .src(src), .dst(dst), .wh(wh),
        .src_stride(src_stride), .dst_stride(dst_stride), .color(color),
        .cmd_valid(cmd_valid), .op(op), .color_out(color_out),
        .cache_control(cache_control), .cache_row(cache_row),
        .cache_last_batch(cache_last_batch), .scale_q26(scale_q26),
        .scale_src_base(scale_src_base), .scale_src_stride(scale_src_stride),
        .read_cmd_valid(read_cmd_valid), .read_cmd_ready(1'b1),
        .read_cmd_addr(read_cmd_addr), .read_cmd_length_bytes(read_cmd_length_bytes),
        .read_cmd_stride_bytes(read_cmd_stride_bytes), .read_cmd_rows(read_cmd_rows),
        .read_busy(1'b0), .read_done(read_done), .read_error(1'b0),
        .dst_read_cmd_valid(dst_read_cmd_valid), .dst_read_cmd_ready(1'b1),
        .dst_read_cmd_addr(dst_read_cmd_addr),
        .dst_read_cmd_length_bytes(dst_read_cmd_length_bytes),
        .dst_read_cmd_stride_bytes(dst_read_cmd_stride_bytes),
        .dst_read_cmd_rows(dst_read_cmd_rows), .dst_read_busy(1'b0),
        .dst_read_done(1'b0), .dst_read_error(1'b0),
        .write_cmd_valid(write_cmd_valid), .write_cmd_ready(1'b1),
        .write_cmd_addr(write_cmd_addr), .write_cmd_length_bytes(write_cmd_length_bytes),
        .write_cmd_last(write_cmd_last), .write_busy(1'b0), .write_done(write_done),
        .write_error(1'b0), .bitblt_busy(bitblt_busy), .bitblt_done(bitblt_done),
        .bitblt_error(bitblt_error)
    );

    always @(posedge clk) begin
        if (!rst_n) begin
            read_done <= 1'b0;
            write_done <= 1'b0;
            write_delay <= 0;
            read_count <= 0;
            errors <= 0;
        end else begin
            read_done <= 1'b0;
            write_done <= 1'b0;
            if (read_cmd_valid) begin
                case (read_count)
                    0: if (read_cmd_addr !== 32'h0010_0000) errors <= errors + 1;
                    1: if (read_cmd_addr !== 32'h0010_0100) errors <= errors + 1;
                    2: if (read_cmd_addr !== 32'h0010_0300) errors <= errors + 1;
                    default: errors <= errors + 1;
                endcase
                read_count <= read_count + 1;
                read_done <= 1'b1;
            end
            if (write_cmd_valid) begin
                write_delay <= 3;
                if (write_cmd_last !== (read_count == 3))
                    errors <= errors + 1;
            end else if (write_delay != 0) begin
                write_delay <= write_delay - 1;
                if (write_delay == 1)
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
        if (read_count != 3 || errors != 0 || bitblt_error || bitblt_busy)
            $fatal(1, "scale address pipeline failed reads=%0d errors=%0d", read_count, errors);
        $display("PASS: pipelined scaler source addresses 0x100000, 0x100100, 0x100300");
        $finish;
    end

    initial begin
        #100000;
        $fatal(1, "timeout");
    end
endmodule
