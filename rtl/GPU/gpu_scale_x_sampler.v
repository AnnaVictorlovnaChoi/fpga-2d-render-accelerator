`timescale 1ns/1ps

// 最近邻横向采样器。
// x_accum 为 Q18.6：高位是源 FIFO 像素下标，低 6 位是小数相位。
// 每产生一个目标像素，x_accum 增加 Q2.6 缩放因子；未采样源像素被丢弃，
// 当前命中的源像素保存在 current_pixel 中，并可在放大时重复输出。
module gpu_scale_x_sampler (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire        line_start,
    input  wire [7:0]  scale_q26,
    input  wire [15:0] dst_width,
    input  wire        src_fifo_empty,
    input  wire [15:0] src_fifo_data,
    output wire        src_fifo_rd_en,
    output wire        out_valid,
    input  wire        out_ready,
    output wire [15:0] out_data,
    output wire        line_done
);
    reg [25:0] x_accum;
    reg [19:0] src_index;
    reg [15:0] dst_index;
    reg [15:0] current_pixel;
    reg current_valid;
    reg line_active;
    reg [7:0] line_scale;
    reg [15:0] line_width;

    wire [19:0] wanted_index = x_accum[25:6];
    wire [25:0] next_x_accum = x_accum + line_scale;
    wire [19:0] next_wanted_index = next_x_accum[25:6];
    wire emit = enable && line_active && current_valid && out_ready &&
                (dst_index < line_width);
    // 只在已启动且尚未完成的目标行内读取源 FIFO。
    wire need_source = enable && line_active && (dst_index < line_width) &&
                       !src_fifo_empty && !emit &&
                       (!current_valid || (src_index <= wanted_index &&
                                            src_index != wanted_index));

    assign src_fifo_rd_en = need_source;
    assign out_valid = enable && line_active && current_valid &&
                       (dst_index < line_width);
    assign out_data = current_pixel;
    assign line_done = enable &&
                       ((line_active && (dst_index >= line_width) && !current_valid) ||
                        (emit && (dst_index + 1'b1 >= line_width)));

    always @(posedge clk) begin
        if (!rst_n) begin
            x_accum <= 26'd0;
            src_index <= 20'd0;
            dst_index <= 16'd0;
            current_pixel <= 16'd0;
            current_valid <= 1'b0;
            line_active <= 1'b0;
            line_scale <= 8'd0;
            line_width <= 16'd0;
        end else if (!enable) begin
            x_accum <= 26'd0;
            src_index <= 20'd0;
            dst_index <= 16'd0;
            current_valid <= 1'b0;
            line_active <= 1'b0;
            line_scale <= 8'd0;
            line_width <= 16'd0;
        end else if (line_start) begin
            x_accum <= 26'd0;
            src_index <= 20'd0;
            dst_index <= 16'd0;
            current_valid <= 1'b0;
            line_scale <= scale_q26;
            line_width <= dst_width;
            line_active <= (dst_width != 0) && (scale_q26 != 0);
        end else begin
            if (src_fifo_rd_en) begin
                current_pixel <= src_fifo_data;
                // The FIFO read data is sampled on this edge. When a downscale
                // step skips source pixels, latch the first pixel at or beyond
                // the requested index instead of waiting for an exact match.
                current_valid <= (src_index >= wanted_index) || current_valid;
                src_index <= src_index + 1'b1;
            end
            if (emit) begin
                dst_index <= dst_index + 1'b1;
                x_accum <= next_x_accum;
                // 放大时继续保留当前像素供后续目标像素重复使用；
                // 缩小时仅当下一采样点越过当前源像素后才丢弃它。
                if (next_wanted_index > src_index - 1'b1)
                    current_valid <= 1'b0;
                if (dst_index + 1'b1 >= line_width) begin
                    current_valid <= 1'b0;
                    line_active <= 1'b0;
                end
            end
        end
    end
endmodule
