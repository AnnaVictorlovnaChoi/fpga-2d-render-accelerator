`timescale 1ns/1ps

// 接收完整的七个参数后，按行产生一次读请求和一次写请求。
// ctrl[10:8]为op：0搬运，1纯色填充，2精灵图，3 alpha混合，4最近邻缩放，5颜色键alpha混合。
module bitblt_cmd_parser #(
    parameter integer ADDR_WIDTH = 32
) (
    input wire clk, input wire rst_n,
    input wire [31:0] ctrl,
    input wire [31:0] src,
    input wire [31:0] dst,
    input wire [31:0] wh,
    input wire [31:0] src_stride,
    input wire [31:0] dst_stride,
    input wire [31:0] color,
    input wire cmd_valid,
    output wire [2:0]op,
    output wire [15:0]color_out,
    output wire [3:0] cache_control,
    output wire [15:0] cache_row,
    output wire cache_last_batch,
    output wire [7:0]scale_q26,
    output wire [ADDR_WIDTH-1:0] scale_src_base,
    output wire [31:0] scale_src_stride,
    output wire read_cmd_valid, input wire read_cmd_ready,
    output wire [ADDR_WIDTH-1:0] read_cmd_addr,
    output wire [31:0] read_cmd_length_bytes,
    output wire [31:0] read_cmd_stride_bytes,
    output wire [6:0] read_cmd_rows,
    input wire read_busy, input wire read_done, input wire read_error,
    output wire dst_read_cmd_valid, input wire dst_read_cmd_ready,
    output wire [ADDR_WIDTH-1:0] dst_read_cmd_addr,
    output wire [31:0] dst_read_cmd_length_bytes,
    output wire [31:0] dst_read_cmd_stride_bytes,
    output wire [6:0] dst_read_cmd_rows,
    input wire dst_read_busy, input wire dst_read_done, input wire dst_read_error,
    output wire write_cmd_valid, input wire write_cmd_ready,
    output wire [ADDR_WIDTH-1:0] write_cmd_addr,
    output wire [31:0] write_cmd_length_bytes,
    output wire write_cmd_last,
    input wire write_busy, input wire write_done, input wire write_error,
    output reg bitblt_busy, output reg bitblt_done, output reg bitblt_error
);
    localparam [3:0] ST_IDLE=4'd0, ST_ISSUE=4'd1, ST_WAIT=4'd2,
                     ST_ERROR=4'd3, ST_COLOR=4'd4, ST_SPRITE=4'd5,
                     ST_SCALE_LEN_MUL=4'd6, ST_SCALE_LEN_ADD=4'd7,
                     ST_BATCH_WRITE=4'd8, ST_BATCH_CALC=4'd9,
                     ST_SCALE_ROW_MUL=4'd10, ST_SCALE_ADDR_MUL=4'd11;
    reg [3:0] state;
    reg [31:0] ctrl_reg, src_reg, dst_reg, wh_reg, src_stride_reg;
    reg [31:0] dst_stride_reg, color_reg;
    // Target row address is advanced once per completed row. Keeping it
    // registered removes the row_index * dst_stride_reg multiplier from the
    // parser-to-writer command path.
    reg [ADDR_WIDTH-1:0] row_dst_addr_reg;
    reg [7:0] scale_reg;
    reg [15:0] scale_width_minus1_reg;
    reg [31:0] scale_read_length_reg;
    reg [23:0] scale_length_product_reg;
    reg [15:0] row_index;
    reg [17:0] scale_src_y_reg;
    reg [ADDR_WIDTH-1:0] scale_src_offset_reg;
    reg read_sent, dst_read_sent, write_sent;
    reg [6:0] batch_rows_reg, batch_rows_left;
    reg [6:0] batch_calc_rows, batch_calc_bit;
    reg [21:0] batch_calc_addend;
    reg [10:0] batch_calc_remaining;
    reg [15:0] batch_calc_height;
    reg read_seen, dst_read_seen, write_seen;
    reg read_valid_reg, dst_read_valid_reg, write_valid_reg;
    wire [2:0] operation = ctrl_reg[10:8];
    wire [15:0] width_pixels = wh_reg[15:0];
    wire [15:0] height_pixels = wh_reg[31:16];
    wire [31:0] row_bytes = {16'd0,width_pixels} << 1;
    wire [21:0] batch_calc_addend_next = batch_calc_addend >> 1;
    wire batch_calc_take = (batch_calc_addend <= {11'd0,batch_calc_remaining});
    wire [6:0] batch_calc_rows_next = batch_calc_rows |
                                      (batch_calc_take ? batch_calc_bit : 7'd0);
    wire [10:0] batch_calc_remaining_next = batch_calc_take ?
                                      batch_calc_remaining-batch_calc_addend[10:0] :
                                      batch_calc_remaining;
    // Split the scaler's row and stride multiplications across clocked states.
    wire [ADDR_WIDTH-1:0] row_src_addr = (operation == 3'd4) ?
                                         (src_reg + scale_src_offset_reg) :
                                         (src_reg + row_index * src_stride_reg);

    assign op=operation;
    assign read_cmd_valid = read_valid_reg;
    assign read_cmd_addr = row_src_addr;
    assign read_cmd_length_bytes = (operation == 3'd4) ? scale_read_length_reg : row_bytes;
    assign read_cmd_stride_bytes = src_stride_reg;
    assign read_cmd_rows = (operation == 3'd4) ? 7'd1 : batch_rows_reg;
    assign dst_read_cmd_valid = dst_read_valid_reg;
    assign dst_read_cmd_addr = row_dst_addr_reg;
    assign dst_read_cmd_length_bytes = row_bytes;
    assign dst_read_cmd_stride_bytes = dst_stride_reg;
    assign dst_read_cmd_rows = batch_rows_reg;
    assign write_cmd_valid = write_valid_reg;
    assign write_cmd_addr = row_dst_addr_reg;
    assign write_cmd_length_bytes = row_bytes;
    assign write_cmd_last = (row_index + 16'd1 >= height_pixels);
    assign color_out=color_reg[15:0];
    assign cache_control = color_reg[31:28];
    assign cache_row = row_index;
    assign cache_last_batch = ({1'b0,row_index} + {10'd0,batch_rows_reg} >= {1'b0,height_pixels});
    assign scale_q26=scale_reg;
    assign scale_src_base=src_reg[ADDR_WIDTH-1:0];
    assign scale_src_stride=src_stride_reg;
    always @(posedge clk) begin
        if (!rst_n) begin
            state<=ST_IDLE; ctrl_reg<=0; src_reg<=0; dst_reg<=0; wh_reg<=0;
            src_stride_reg<=0; dst_stride_reg<=0; color_reg<=0; scale_reg<=0; scale_width_minus1_reg<=0; scale_read_length_reg<=0; scale_length_product_reg<=0; row_dst_addr_reg<=0; row_index<=0; scale_src_y_reg<=0; scale_src_offset_reg<=0;
            read_sent<=0; dst_read_sent<=0; write_sent<=0;
            batch_rows_reg<=0; batch_rows_left<=0; batch_calc_rows<=0;
            batch_calc_bit<=0; batch_calc_addend<=0; batch_calc_remaining<=0;
            batch_calc_height<=0;
            read_seen<=0; dst_read_seen<=0; write_seen<=0;
            read_valid_reg<=0; dst_read_valid_reg<=0; write_valid_reg<=0;
            bitblt_busy<=0; bitblt_done<=0; bitblt_error<=0;
        end else begin
            bitblt_done <= 1'b0;
            if (read_valid_reg && read_cmd_ready) begin read_valid_reg<=0; read_sent<=1; end
            if (dst_read_valid_reg && dst_read_cmd_ready) begin dst_read_valid_reg<=0; dst_read_sent<=1; end
            if (write_valid_reg && write_cmd_ready) begin write_valid_reg<=0; write_sent<=1; end
            if (read_done) read_seen<=1;
            if (dst_read_done) dst_read_seen<=1;
            if (write_done) write_seen<=1;
            if (read_error || dst_read_error || write_error) begin
                read_valid_reg<=0; dst_read_valid_reg<=0; write_valid_reg<=0;
                bitblt_error<=1; bitblt_busy<=0; state<=ST_ERROR;
            end
            case (state)
                ST_IDLE: begin
                    bitblt_busy<=0; bitblt_error<=0;
                    if (cmd_valid) begin
                        ctrl_reg<=ctrl;
                        src_reg<=src;
                        dst_reg<=dst;
                        row_dst_addr_reg<=dst[ADDR_WIDTH-1:0];
                        wh_reg<=wh;
                        src_stride_reg<=src_stride;
                        dst_stride_reg<=dst_stride;
                        color_reg<=color;
                        if (ctrl[10:8] == 3'd4) begin
                            scale_reg<=color[7:0];
                            scale_width_minus1_reg<=wh[15:0] - 16'd1;
                            scale_read_length_reg <= 32'd0;
                            state <= ST_SCALE_LEN_MUL;
                        end else begin
                            scale_read_length_reg <= 32'd0;
                        end
                        bitblt_busy<=1;
                        if (!ctrl[0] || wh[15:0] == 0 ||
                            wh[31:16] == 0 || dst_stride == 0 ||
                            (ctrl[10:8] == 3'd0 && src_stride == 0) ||
                            ((ctrl[10:8] == 3'd2 || ctrl[10:8] == 3'd3||ctrl[10:8] == 3'd5) && src_stride == 0))begin
                        bitblt_error<=1'b1;
                        bitblt_busy<=1'b0;
                        state<=ST_ERROR;
                        end else if(ctrl[10:8] == 3'd0)begin//像素搬运
                            row_index<=0; read_sent<=0; dst_read_sent<=1; write_sent<=0;
                            batch_calc_rows<=0; batch_calc_bit<=7'd64;
                            batch_calc_addend<={5'd0,wh[15:0],6'd0};
                            batch_calc_remaining<=11'd1024; batch_calc_height<=wh[31:16];
                            read_seen<=0; dst_read_seen<=1; write_seen<=0; state<=ST_BATCH_CALC;
                        end else if(ctrl[10:8] == 3'd2 || ctrl[10:8] == 3'd3 || ctrl[10:8] == 3'd5)begin//精灵图、alpha及颜色键alpha混合均需读取源图和目标图
                            row_index<=0; read_sent<=0; dst_read_sent<=0; write_sent<=0;
                            batch_calc_rows<=0; batch_calc_bit<=7'd64;
                            batch_calc_addend<={5'd0,wh[15:0],6'd0};
                            batch_calc_remaining<=11'd1024; batch_calc_height<=wh[31:16];
                            read_seen<=0; dst_read_seen<=0; write_seen<=0; state<=ST_BATCH_CALC;
                        end else if(ctrl[10:8] == 3'd1)begin//纯色填充，不用read，默认设置为1就行
                            row_index<=0; read_sent<=1; dst_read_sent<=1; write_sent<=0;
                        read_seen<=1; dst_read_seen<=1; write_seen<=0; state<=ST_COLOR;
                        end else if (ctrl[10:8] == 3'd4) begin
                            if (color[7:0] == 0) begin
                                bitblt_error<=1'b1;
                                bitblt_busy<=1'b0;
                                state<=ST_ERROR;
                            end
                        end else begin
                        bitblt_error<=1'b1;
                        bitblt_busy<=1'b0;
                        state<=ST_ERROR;
                        end
                    end
                end
                ST_SCALE_LEN_MUL: begin
                    scale_length_product_reg <=
                        scale_width_minus1_reg * {16'd0, scale_reg};
                    state <= ST_SCALE_LEN_ADD;
                end
                ST_BATCH_CALC: begin
                    batch_calc_rows<=batch_calc_rows_next;
                    batch_calc_remaining<=batch_calc_remaining_next;
                    batch_calc_addend<=batch_calc_addend_next;
                    batch_calc_bit<=batch_calc_bit >> 1;
                    if (batch_calc_bit == 1) begin
                        if (batch_calc_rows_next == 0) begin
                            batch_rows_reg<=7'd1; batch_rows_left<=7'd1;
                        end else if ({9'd0,batch_calc_rows_next} > batch_calc_height) begin
                            batch_rows_reg<=batch_calc_height[6:0];
                            batch_rows_left<=batch_calc_height[6:0];
                        end else begin
                            batch_rows_reg<=batch_calc_rows_next;
                            batch_rows_left<=batch_calc_rows_next;
                        end
                        read_sent<=0; read_seen<=0;
                        if (operation==3'd0) begin
                            dst_read_sent<=1; dst_read_seen<=1; state<=ST_ISSUE;
                        end else begin
                            dst_read_sent<=0; dst_read_seen<=0; state<=ST_SPRITE;
                        end
                        write_sent<=0; write_seen<=0;
                    end
                end
                ST_SCALE_LEN_ADD: begin
                    scale_read_length_reg <=
                        {12'd0, (scale_length_product_reg[23:6] + 18'd1), 1'b0};
                    if (scale_reg == 0) begin
                        bitblt_error <= 1'b1;
                        bitblt_busy <= 1'b0;
                        state <= ST_ERROR;
                    end else begin
                        row_index<=0; scale_src_y_reg<=0; scale_src_offset_reg<=0; read_sent<=0; dst_read_sent<=1; write_sent<=0;
                        batch_rows_reg<=7'd1; batch_rows_left<=7'd1;
                        read_seen<=0; dst_read_seen<=1; write_seen<=0; state<=ST_SCALE_ROW_MUL;
                    end
                end
                ST_SCALE_ROW_MUL: begin
                    scale_src_y_reg <=
                        ({8'd0,row_index} * {16'd0,scale_reg}) >> 6;
                    state <= ST_SCALE_ADDR_MUL;
                end
                ST_SCALE_ADDR_MUL: begin
                    scale_src_offset_reg <= scale_src_y_reg * src_stride_reg;
                    state <= ST_ISSUE;
                end
                ST_ISSUE: begin
                    if (!read_valid_reg && !read_sent) read_valid_reg<=1;
                    if (read_sent && read_seen &&
                        ((operation == 3'd0) || (operation == 3'd4)))
                        state<=ST_BATCH_WRITE;
                end
                ST_COLOR: begin
                    if (!write_sent && !write_valid_reg) write_valid_reg<=1;
                    if (write_sent) state<=ST_WAIT;
                end
                ST_SPRITE: begin
                    if (!read_sent && !read_valid_reg) read_valid_reg<=1;
                    if (!dst_read_sent && !dst_read_valid_reg) dst_read_valid_reg<=1;
                    if (read_sent && dst_read_sent && read_seen && dst_read_seen) begin
                        state<=ST_BATCH_WRITE;
                    end
                end
                ST_BATCH_WRITE: begin
                    if (!write_sent && !write_valid_reg) write_valid_reg<=1;
                    if (write_sent) state<=ST_WAIT;
                end
                ST_WAIT: if (write_seen) begin
                    if (operation == 3'd1) begin
                        if (row_index + 1'b1 >= height_pixels) begin
                            bitblt_busy<=0;
                            bitblt_done<=1;
                            state<=ST_IDLE;
                        end else begin
                            row_index<=row_index+1'b1;
                            row_dst_addr_reg<=row_dst_addr_reg + dst_stride_reg[ADDR_WIDTH-1:0];
                            write_sent<=0;
                            write_seen<=0;
                            read_sent<=1; dst_read_sent<=1;
                            read_seen<=1; dst_read_seen<=1;
                            state<=ST_COLOR;
                        end
                    end else if (batch_rows_left > 1) begin
                        batch_rows_left<=batch_rows_left-1'b1;
                        row_index<=row_index+1'b1;
                        row_dst_addr_reg<=row_dst_addr_reg + dst_stride_reg[ADDR_WIDTH-1:0];
                        write_sent<=0;
                        write_seen<=0;
                        state<=ST_BATCH_WRITE;
                    end else if (row_index + 1'b1 >= height_pixels) begin
                        bitblt_busy<=0; bitblt_done<=1; state<=ST_IDLE;
                    end else begin
                        row_index<=row_index+1'b1;
                        row_dst_addr_reg<=row_dst_addr_reg + dst_stride_reg[ADDR_WIDTH-1:0];
                        write_sent<=0;
                        write_seen<=0;
                        if (operation == 3'd4) begin
                            batch_rows_reg<=7'd1;
                            batch_rows_left<=7'd1;
                            read_sent<=0; read_seen<=0;
                            dst_read_sent<=1; dst_read_seen<=1;
                            state<=ST_SCALE_ROW_MUL;
                        end else begin
                        batch_calc_rows<=0; batch_calc_bit<=7'd64;
                        batch_calc_addend<={5'd0,width_pixels,6'd0};
                        batch_calc_remaining<=11'd1024;
                        batch_calc_height<=height_pixels-(row_index+1'b1);
                        state<=ST_BATCH_CALC;
                        end
                    end
                end
                ST_ERROR: begin bitblt_busy<=0; state<=ST_IDLE; end
                default: state<=ST_IDLE;
            endcase
        end
    end
    wire unused_status = read_busy ^ write_busy ^ color_reg[0] ^ scale_reg[0];
endmodule

// Packed-bus wrapper used by the 8-core GPU top.  Every parser owns its complete command
// state and its three local DMA request channels.
module bitblt_cmd_parser8 #(
    parameter integer CORE_COUNT = 8,
    parameter integer ADDR_WIDTH = 32
) (
    input wire clk, input wire rst_n,
    input wire [31:0] ctrl, input wire [31:0] src,
    input wire [31:0] dst, input wire [31:0] wh,
    input wire [31:0] src_stride, input wire [31:0] dst_stride,
    input wire [31:0] color,
    input wire [CORE_COUNT-1:0] cmd_valid,
    output wire [CORE_COUNT*3-1:0] op,
    output wire [CORE_COUNT*16-1:0] color_out,
    output wire [CORE_COUNT*4-1:0] cache_control,
    output wire [CORE_COUNT*16-1:0] cache_row,
    output wire [CORE_COUNT-1:0] cache_last_batch,
    output wire [CORE_COUNT*8-1:0] scale_q26,
    output wire [CORE_COUNT*ADDR_WIDTH-1:0] scale_src_base,
    output wire [CORE_COUNT*32-1:0] scale_src_stride,
    output wire [CORE_COUNT-1:0] read_cmd_valid,
    input wire [CORE_COUNT-1:0] read_cmd_ready,
    output wire [CORE_COUNT*ADDR_WIDTH-1:0] read_cmd_addr,
    output wire [CORE_COUNT*32-1:0] read_cmd_length_bytes,
    output wire [CORE_COUNT*32-1:0] read_cmd_stride_bytes,
    output wire [CORE_COUNT*7-1:0] read_cmd_rows,
    input wire [CORE_COUNT-1:0] read_busy, input wire [CORE_COUNT-1:0] read_done,
    input wire [CORE_COUNT-1:0] read_error,
    output wire [CORE_COUNT-1:0] dst_read_cmd_valid,
    input wire [CORE_COUNT-1:0] dst_read_cmd_ready,
    output wire [CORE_COUNT*ADDR_WIDTH-1:0] dst_read_cmd_addr,
    output wire [CORE_COUNT*32-1:0] dst_read_cmd_length_bytes,
    output wire [CORE_COUNT*32-1:0] dst_read_cmd_stride_bytes,
    output wire [CORE_COUNT*7-1:0] dst_read_cmd_rows,
    input wire [CORE_COUNT-1:0] dst_read_busy, input wire [CORE_COUNT-1:0] dst_read_done,
    input wire [CORE_COUNT-1:0] dst_read_error,
    output wire [CORE_COUNT-1:0] write_cmd_valid,
    input wire [CORE_COUNT-1:0] write_cmd_ready,
    output wire [CORE_COUNT*ADDR_WIDTH-1:0] write_cmd_addr,
    output wire [CORE_COUNT*32-1:0] write_cmd_length_bytes,
    output wire [CORE_COUNT-1:0] write_cmd_last,
    input wire [CORE_COUNT-1:0] write_busy, input wire [CORE_COUNT-1:0] write_done,
    input wire [CORE_COUNT-1:0] write_error,
    output wire [CORE_COUNT-1:0] bitblt_busy,
    output wire [CORE_COUNT-1:0] bitblt_done,
    output wire [CORE_COUNT-1:0] bitblt_error
);
    genvar i;
    generate for (i=0; i<CORE_COUNT; i=i+1) begin: PARSER
        bitblt_cmd_parser #(.ADDR_WIDTH(ADDR_WIDTH)) u_parser(
            .clk(clk),.rst_n(rst_n),.ctrl(ctrl),.src(src),.dst(dst),.wh(wh),
            .src_stride(src_stride),.dst_stride(dst_stride),.color(color),
            .cmd_valid(cmd_valid[i]),.op(op[i*3 +: 3]),.color_out(color_out[i*16 +: 16]),
            .cache_control(cache_control[i*4 +: 4]),.cache_row(cache_row[i*16 +: 16]),
            .cache_last_batch(cache_last_batch[i]),
            .scale_q26(scale_q26[i*8 +: 8]),
            .scale_src_base(scale_src_base[i*ADDR_WIDTH +: ADDR_WIDTH]),
            .scale_src_stride(scale_src_stride[i*32 +: 32]),
            .read_cmd_valid(read_cmd_valid[i]),.read_cmd_ready(read_cmd_ready[i]),
            .read_cmd_addr(read_cmd_addr[i*ADDR_WIDTH +: ADDR_WIDTH]),.read_cmd_length_bytes(read_cmd_length_bytes[i*32 +: 32]),
            .read_cmd_stride_bytes(read_cmd_stride_bytes[i*32 +: 32]),.read_cmd_rows(read_cmd_rows[i*7 +: 7]),
            .read_busy(read_busy[i]),.read_done(read_done[i]),.read_error(read_error[i]),
            .dst_read_cmd_valid(dst_read_cmd_valid[i]),.dst_read_cmd_ready(dst_read_cmd_ready[i]),
            .dst_read_cmd_addr(dst_read_cmd_addr[i*ADDR_WIDTH +: ADDR_WIDTH]),.dst_read_cmd_length_bytes(dst_read_cmd_length_bytes[i*32 +: 32]),
            .dst_read_cmd_stride_bytes(dst_read_cmd_stride_bytes[i*32 +: 32]),.dst_read_cmd_rows(dst_read_cmd_rows[i*7 +: 7]),
            .dst_read_busy(dst_read_busy[i]),.dst_read_done(dst_read_done[i]),.dst_read_error(dst_read_error[i]),
            .write_cmd_valid(write_cmd_valid[i]),.write_cmd_ready(write_cmd_ready[i]),
            .write_cmd_addr(write_cmd_addr[i*ADDR_WIDTH +: ADDR_WIDTH]),.write_cmd_length_bytes(write_cmd_length_bytes[i*32 +: 32]),
            .write_cmd_last(write_cmd_last[i]),
            .write_busy(write_busy[i]),.write_done(write_done[i]),.write_error(write_error[i]),
            .bitblt_busy(bitblt_busy[i]),.bitblt_done(bitblt_done[i]),.bitblt_error(bitblt_error[i]));
    end endgenerate
endmodule
