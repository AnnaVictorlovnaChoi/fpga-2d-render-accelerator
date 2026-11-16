`timescale 1ns/1ps

module tb_parser_colorfill;
    localparam integer CORE_COUNT = 2;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst_n = 1'b0;
    reg fifo_wr_en = 1'b0;
    reg [31:0] fifo_wr_data = 0;
    wire fifo_rd_en, fifo_empty, fifo_full, fifo_prog_full;
    wire [31:0] fifo_rd_data;
    wire [7:0] fifo_count;
    wire [31:0] ctrl, src, dst, wh, src_stride, dst_stride, color;
    wire scheduler_busy;
    wire [CORE_COUNT-1:0] parser_cmd_valid;
    wire [CORE_COUNT*3-1:0] op;
    wire [CORE_COUNT*16-1:0] color_out;
    wire [CORE_COUNT*8-1:0] scale_q26;
    wire [CORE_COUNT*32-1:0] scale_src_base, scale_src_stride;
    wire [CORE_COUNT-1:0] read_cmd_valid, dst_read_cmd_valid, write_cmd_valid;
    wire [CORE_COUNT-1:0] read_busy, read_done, read_error;
    wire [CORE_COUNT-1:0] dst_read_busy, dst_read_done, dst_read_error;
    wire [CORE_COUNT-1:0] write_busy, write_done, write_error;
    wire [CORE_COUNT-1:0] bitblt_busy, bitblt_done, bitblt_error;
    wire [CORE_COUNT-1:0] read_cmd_ready = {CORE_COUNT{1'b1}};
    wire [CORE_COUNT-1:0] dst_read_cmd_ready = {CORE_COUNT{1'b1}};
    wire [CORE_COUNT-1:0] write_cmd_ready;
    wire [CORE_COUNT*32-1:0] read_cmd_addr, read_cmd_length_bytes, read_cmd_stride_bytes;
    wire [CORE_COUNT*7-1:0] read_cmd_rows;
    wire [CORE_COUNT*32-1:0] dst_read_cmd_addr, dst_read_cmd_length_bytes, dst_read_cmd_stride_bytes;
    wire [CORE_COUNT*7-1:0] dst_read_cmd_rows;
    wire [CORE_COUNT*32-1:0] write_cmd_addr, write_cmd_length_bytes;
    reg [CORE_COUNT-1:0] mock_writer_busy = 0;
    reg [CORE_COUNT-1:0] mock_writer_done = 0;
    reg [3:0] delay0 = 0, delay1 = 0;
    integer done_count = 0;
    integer write_accept_count = 0;
    integer failures = 0;
    integer k;

    gpu_sync_fifo #(.WIDTH(32), .DEPTH(128), .PROG_FULL_LEVEL(120)) command_fifo (
        .clk(clk), .rst_n(rst_n), .wr_en(fifo_wr_en), .wr_data(fifo_wr_data),
        .wr_keep(1'b1), .rd_en(fifo_rd_en), .rd_data(fifo_rd_data),
        .full(fifo_full), .prog_full(fifo_prog_full), .empty(fifo_empty),
        .count_used(fifo_count)
    );

    gpu_schduler8 #(.CORE_COUNT(CORE_COUNT)) scheduler (
        .clk(clk), .rst_n(rst_n), .cmd_fifo_empty(fifo_empty),
        .cmd_fifo_word_count(fifo_count), .cmd_fifo_rd_en(fifo_rd_en),
        .cmd_fifo_rd_data(fifo_rd_data), .ctrl(ctrl), .src(src), .dst(dst),
        .wh(wh), .src_stride(src_stride), .dst_stride(dst_stride), .color(color),
        .parser_ready(~bitblt_busy), .parser_busy(bitblt_busy),
        .parser_cmd_valid(parser_cmd_valid), .scheduler_busy(scheduler_busy)
    );

    bitblt_cmd_parser8 #(.CORE_COUNT(CORE_COUNT), .ADDR_WIDTH(32)) parsers (
        .clk(clk), .rst_n(rst_n), .ctrl(ctrl), .src(src), .dst(dst), .wh(wh),
        .src_stride(src_stride), .dst_stride(dst_stride), .color(color),
        .cmd_valid(parser_cmd_valid), .op(op), .color_out(color_out),
        .scale_q26(scale_q26), .scale_src_base(scale_src_base),
        .scale_src_stride(scale_src_stride), .read_cmd_valid(read_cmd_valid),
        .read_cmd_ready(read_cmd_ready), .read_cmd_addr(read_cmd_addr),
        .read_cmd_length_bytes(read_cmd_length_bytes),
        .read_cmd_stride_bytes(read_cmd_stride_bytes), .read_cmd_rows(read_cmd_rows),
        .read_busy(read_busy), .read_done(read_done), .read_error(read_error),
        .dst_read_cmd_valid(dst_read_cmd_valid), .dst_read_cmd_ready(dst_read_cmd_ready),
        .dst_read_cmd_addr(dst_read_cmd_addr),
        .dst_read_cmd_length_bytes(dst_read_cmd_length_bytes),
        .dst_read_cmd_stride_bytes(dst_read_cmd_stride_bytes),
        .dst_read_cmd_rows(dst_read_cmd_rows), .dst_read_busy(dst_read_busy),
        .dst_read_done(dst_read_done), .dst_read_error(dst_read_error),
        .write_cmd_valid(write_cmd_valid), .write_cmd_ready(write_cmd_ready),
        .write_cmd_addr(write_cmd_addr), .write_cmd_length_bytes(write_cmd_length_bytes),
        .write_busy(write_busy), .write_done(write_done), .write_error(write_error),
        .bitblt_busy(bitblt_busy), .bitblt_done(bitblt_done), .bitblt_error(bitblt_error)
    );

    assign write_cmd_ready[0] = !mock_writer_busy[0];
    assign write_cmd_ready[1] = !mock_writer_busy[1];
    assign write_busy = mock_writer_busy;
    assign write_done = mock_writer_done;
    assign write_error = {CORE_COUNT{1'b0}};
    assign read_busy = {CORE_COUNT{1'b0}};
    assign read_done = {CORE_COUNT{1'b0}};
    assign read_error = {CORE_COUNT{1'b0}};
    assign dst_read_busy = {CORE_COUNT{1'b0}};
    assign dst_read_done = {CORE_COUNT{1'b0}};
    assign dst_read_error = {CORE_COUNT{1'b0}};

    always @(posedge clk) begin
        if (!rst_n) begin
            mock_writer_busy <= 0;
            // Keep done asserted for the full cycle after completion so the
            // parser samples the same synchronous pulse as the real writer.
            mock_writer_done <= 0;
            delay0 <= 0;
            delay1 <= 0;
            done_count <= 0;
            write_accept_count <= 0;
        end else begin
            mock_writer_done <= 0;
            if (write_cmd_valid[0] && write_cmd_ready[0]) begin
                mock_writer_busy[0] <= 1'b1;
                delay0 <= 4'd3;
                write_accept_count <= write_accept_count + 1;
            end else if (mock_writer_busy[0]) begin
                if (delay0 == 0) begin
                    mock_writer_busy[0] <= 1'b0;
                    mock_writer_done[0] <= 1'b1;
                end else delay0 <= delay0 - 1'b1;
            end
            if (write_cmd_valid[1] && write_cmd_ready[1]) begin
                mock_writer_busy[1] <= 1'b1;
                delay1 <= 4'd3;
                write_accept_count <= write_accept_count + 1;
            end else if (mock_writer_busy[1]) begin
                if (delay1 == 0) begin
                    mock_writer_busy[1] <= 1'b0;
                    mock_writer_done[1] <= 1'b1;
                end else delay1 <= delay1 - 1'b1;
            end
            if (|bitblt_done)
                done_count <= done_count + 1;
            if (|bitblt_error) begin
                $display("ERROR: parser error busy=%b op=%b", bitblt_busy, op);
                failures <= failures + 1;
            end
        end
    end

    always @(posedge clk) begin
        if (rst_n && $time < 400000 && ((|write_cmd_valid) || (|write_done) || (|bitblt_done)))
            $display("TRACE t=%0t sched=%0d valid=%b ready=%b sent={%b,%b} seen={%b,%b} wbusy=%b wdone=%b pstate={%0d,%0d} wh={%08x,%08x} row={%0d,%0d} len={%0d,%0d}",
                     $time, scheduler.state, write_cmd_valid, write_cmd_ready,
                     parsers.PARSER[0].u_parser.write_sent,
                     parsers.PARSER[1].u_parser.write_sent,
                     parsers.PARSER[0].u_parser.write_seen,
                     parsers.PARSER[1].u_parser.write_seen,
                     mock_writer_busy, mock_writer_done,
                     parsers.PARSER[0].u_parser.state,
                     parsers.PARSER[1].u_parser.state,
                     parsers.PARSER[0].u_parser.wh_reg,
                     parsers.PARSER[1].u_parser.wh_reg,
                     parsers.PARSER[0].u_parser.row_index,
                     parsers.PARSER[1].u_parser.row_index,
                     write_cmd_length_bytes[31:0],
                     write_cmd_length_bytes[63:32]);
    end

    task push_word;
        input [31:0] value;
        begin
            @(negedge clk);
            fifo_wr_data = value;
            fifo_wr_en = 1'b1;
            @(negedge clk);
            fifo_wr_en = 1'b0;
        end
    endtask

    task push_colorfill;
        input integer index;
        begin
            push_word(32'h0000_0101); // valid + OP_COLOR
            push_word(0);
            push_word(32'h0600_0000 + index * 32'h0000_1000);
            push_word(32'h0001_0020); // 32 x 1 pixels
            push_word(32'h0000_1000);
            push_word(32'h0000_1000);
            push_word(32'h0000_f800 + index);
        end
    endtask

    initial begin
        repeat (5) @(negedge clk);
        rst_n = 1'b1;
        for (k = 0; k < 12; k = k + 1)
            push_colorfill(k);
        fifo_wr_en = 1'b0;
        repeat (1000) @(posedge clk);
        $display("RESULT commands=12 writer_accepts=%0d parser_done=%0d fifo_count=%0d scheduler_busy=%b parser_busy=%b failures=%0d",
                 write_accept_count, done_count, fifo_count, scheduler_busy, bitblt_busy, failures);
        $display("P0 state=%0d sent=%b seen=%b valid=%b writer_busy=%b writer_done=%b; P1 state=%0d sent=%b seen=%b valid=%b writer_busy=%b writer_done=%b; scheduler_state=%0d",
                 parsers.PARSER[0].u_parser.state,
                 parsers.PARSER[0].u_parser.write_sent,
                 parsers.PARSER[0].u_parser.write_seen,
                 parsers.PARSER[0].u_parser.write_valid_reg,
                 mock_writer_busy[0], mock_writer_done[0],
                 parsers.PARSER[1].u_parser.state,
                 parsers.PARSER[1].u_parser.write_sent,
                 parsers.PARSER[1].u_parser.write_seen,
                 parsers.PARSER[1].u_parser.write_valid_reg,
                 mock_writer_busy[1], mock_writer_done[1],
                 scheduler.state);
        if (write_accept_count != 12 || done_count != 12 || fifo_count != 0 || failures != 0)
            $fatal(1, "colorfill command path failed");
        $finish;
    end
endmodule
