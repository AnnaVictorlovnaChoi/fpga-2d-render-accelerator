`timescale 1ns/1ps

// 最近邻坐标映射组合逻辑。
// scale_q26 为 Q2.6 定点数：src_coord = floor(dst_coord * scale_q26 / 64)。
// 本模块负责坐标映射和源像素字节地址计算；AXI 读写由 DMA 完成，
// FIFO 中的横向跳采样和像素重复由缩放像素数据通路完成。
module gpu_scale_engine #(
    parameter integer ADDR_WIDTH = 32
) (
    input  wire [ADDR_WIDTH-1:0] src_base,
    input  wire [31:0] src_stride_bytes,
    input  wire [15:0] dst_x,
    input  wire [15:0] dst_y,
    input  wire [7:0]  scale_q26,
    output wire        valid,
    output wire [31:0] src_x,
    output wire [31:0] src_y,
    output wire [ADDR_WIDTH-1:0] src_addr
);
    wire [23:0] x_product = {8'd0, dst_x} * {16'd0, scale_q26};
    wire [23:0] y_product = {8'd0, dst_y} * {16'd0, scale_q26};
    wire [31:0] mapped_x = {8'd0, x_product[23:6]};
    wire [31:0] mapped_y = {8'd0, y_product[23:6]};

    assign valid = (scale_q26 != 0);
    assign src_x = mapped_x;
    assign src_y = mapped_y;
    assign src_addr = src_base + mapped_y * src_stride_bytes + (mapped_x << 1);
endmodule
