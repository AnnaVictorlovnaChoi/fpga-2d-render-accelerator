`timescale 1ns/1ps

// GPU 顶层：CPU 命令 FIFO、6 路 bitblt 核心和共享 DMA。
module GPU_top #(
    parameter integer CORE_COUNT=3,
    parameter integer AXI_ADDR_WIDTH=32,
    parameter integer AXI_ID_WIDTH=4,
    parameter integer READ_BURST_LEN=8,
    parameter integer WRITE_BURST_LEN=8,
    parameter integer AXI_DATA_WIDTH=64,
    parameter integer USER_AXI_ID_WIDTH=8
) (
    input wire user_clk,
    input wire io_ddrMasters_1_reset,
    input wire [USER_AXI_ID_WIDTH-1:0] axiA_awid,
    input wire [AXI_ADDR_WIDTH-1:0] axiA_awaddr,
    input wire [7:0] axiA_awlen,
    input wire [2:0] axiA_awsize,
    input wire [1:0] axiA_awburst,
    input wire [3:0] axiA_awcache,
    input wire [3:0] axiA_awqos,
    input wire [3:0] axiA_awregion,
    input wire [2:0] axiA_awprot,
    input wire axiA_awlock,
    input wire axiA_awvalid,
    output wire axiA_awready,
    input wire [31:0] axiA_wdata,
    input wire [3:0] axiA_wstrb,
    input wire axiA_wlast,
    input wire axiA_wvalid,
    output wire axiA_wready,
    output wire [USER_AXI_ID_WIDTH-1:0] axiA_bid,
    output wire [1:0] axiA_bresp,
    output wire axiA_bvalid,
    input wire axiA_bready,
    input wire [USER_AXI_ID_WIDTH-1:0] axiA_arid,
    input wire [AXI_ADDR_WIDTH-1:0] axiA_araddr,
    input wire [7:0] axiA_arlen,
    input wire [2:0] axiA_arsize,
    input wire [1:0] axiA_arburst,
    input wire [3:0] axiA_arcache,
    input wire [3:0] axiA_arqos,
    input wire [3:0] axiA_arregion,
    input wire [2:0] axiA_arprot,
    input wire axiA_arlock,
    input wire axiA_arvalid,
    output wire axiA_arready,
    output wire [USER_AXI_ID_WIDTH-1:0] axiA_rid,
    output wire [31:0] axiA_rdata,
    output wire [1:0] axiA_rresp,
    output wire axiA_rlast,
    output wire axiA_rvalid,
    input wire axiA_rready,
    output wire [AXI_ID_WIDTH-1:0] io_ddrMasters_1_ar_payload_id,
    output wire [AXI_ADDR_WIDTH-1:0] io_ddrMasters_1_ar_payload_addr,
    output wire [7:0] io_ddrMasters_1_ar_payload_len,
    output wire [2:0] io_ddrMasters_1_ar_payload_size,
    output wire [1:0] io_ddrMasters_1_ar_payload_burst,
    output wire [3:0] io_ddrMasters_1_ar_payload_cache,
    output wire [3:0] io_ddrMasters_1_ar_payload_qos,
    output wire [3:0] io_ddrMasters_1_ar_payload_region,
    output wire [2:0] io_ddrMasters_1_ar_payload_prot,
    output wire io_ddrMasters_1_ar_payload_lock,
    output wire io_ddrMasters_1_ar_valid,
    input wire io_ddrMasters_1_ar_ready,
    input wire [AXI_ID_WIDTH-1:0] io_ddrMasters_1_r_payload_id,
    input wire [AXI_DATA_WIDTH-1:0] io_ddrMasters_1_r_payload_data,
    input wire [1:0] io_ddrMasters_1_r_payload_resp,
    input wire io_ddrMasters_1_r_payload_last,
    input wire io_ddrMasters_1_r_valid,
    output wire io_ddrMasters_1_r_ready,
    output wire [AXI_ID_WIDTH-1:0] io_ddrMasters_1_aw_payload_id,
    output wire [AXI_ADDR_WIDTH-1:0] io_ddrMasters_1_aw_payload_addr,
    output wire [7:0] io_ddrMasters_1_aw_payload_len,
    output wire [2:0] io_ddrMasters_1_aw_payload_size,
    output wire [1:0] io_ddrMasters_1_aw_payload_burst,
    output wire [3:0] io_ddrMasters_1_aw_payload_cache,
    output wire [3:0] io_ddrMasters_1_aw_payload_qos,
    output wire [3:0] io_ddrMasters_1_aw_payload_region,
    output wire [2:0] io_ddrMasters_1_aw_payload_prot,
    output wire io_ddrMasters_1_aw_payload_lock,
    output wire io_ddrMasters_1_aw_valid,
    input wire io_ddrMasters_1_aw_ready,
    output wire [AXI_DATA_WIDTH-1:0] io_ddrMasters_1_w_payload_data,
    output wire [(AXI_DATA_WIDTH/8)-1:0] io_ddrMasters_1_w_payload_strb,
    output wire io_ddrMasters_1_w_payload_last,
    output wire io_ddrMasters_1_w_valid,
    input wire io_ddrMasters_1_w_ready,
    input wire [AXI_ID_WIDTH-1:0] io_ddrMasters_1_b_payload_id,
    input wire [1:0] io_ddrMasters_1_b_payload_resp,
    input wire io_ddrMasters_1_b_valid,
    output wire io_ddrMasters_1_b_ready
);
    wire rst_n = ~io_ddrMasters_1_reset;
    wire cmd_wr_en, cmd_rd_en, cmd_full, cmd_prog_full, cmd_empty;
    wire [31:0] cmd_wr_data, cmd_rd_data;
    wire [3:0] cmd_wr_strb;
    wire [CORE_COUNT-1:0] sched_valid;
    wire sched_busy;
    wire [31:0] sched_ctrl, sched_src, sched_dst, sched_wh, sched_ss, sched_ds, sched_color;
    wire [CORE_COUNT-1:0] parser_busy, parser_done, parser_error;
    wire [CORE_COUNT-1:0] read_valid, read_ready, dst_read_valid, dst_read_ready, write_valid, write_ready, write_last;
    wire [CORE_COUNT-1:0] read_busy, read_done, read_error, dst_read_busy, dst_read_done, dst_read_error, write_busy, write_done, write_error;
    wire [CORE_COUNT-1:0] fifo_full, fifo_empty, fifo_prog_full, fifo_wr_en, fifo_rd_en;
    wire [CORE_COUNT-1:0] dst_fifo_full, dst_fifo_empty, dst_fifo_prog_full, dst_fifo_wr_en, dst_fifo_rd_en;
    wire [CORE_COUNT-1:0] write_fifo_empty, write_fifo_rd_en;
    wire [CORE_COUNT-1:0] scale_fifo_empty, scale_fifo_full, scale_fifo_rd_en, scale_fifo_wr_en;
    wire [CORE_COUNT*16-1:0] scale_fifo_data, scale_fifo_wr_data;
    wire [CORE_COUNT*4-1:0] fifo_wr_keep, dst_fifo_wr_keep;
    wire [CORE_COUNT-1:0] fifo_wr_last;
    wire [CORE_COUNT*64-1:0] fifo_wr_data, dst_fifo_wr_data;
    wire [CORE_COUNT*16-1:0] fifo_rd_data, dst_fifo_rd_data;
    wire [CORE_COUNT*3-1:0] op;
    wire [CORE_COUNT*16-1:0] color;
    wire [CORE_COUNT*4-1:0] cache_control;
    wire [CORE_COUNT*16-1:0] cache_row;
    wire [CORE_COUNT-1:0] cache_last_batch;
    wire [CORE_COUNT*8-1:0] scale_q26;
    wire [CORE_COUNT*32-1:0] read_addr, read_len, read_stride, dst_read_addr, dst_read_len, dst_read_stride, write_addr, write_len;
    wire [CORE_COUNT*7-1:0] read_rows, dst_read_rows;
    wire [CORE_COUNT*AXI_ADDR_WIDTH-1:0] dma_read_addr;
    wire [7:0] cmd_word_count;
    reg read_pause_new_bursts_q;

    // Register FIFO high-water backpressure to break the FIFO-count-to-DMA
    // grant path. Assert one burst early to cover the added cycle of latency.
    always @(posedge user_clk or negedge rst_n) begin
        if (!rst_n)
            read_pause_new_bursts_q <= 1'b0;
        else
            read_pause_new_bursts_q <= (|fifo_prog_full) ||
                                       (|dst_fifo_prog_full);
    end
    // 命令按 7 个 32 位字组成一条指令，不能在接近阈值时截断正在写入的指令。
    // 将 prog_full 设为满值，使 AXI 从机只在真正没有空间时停止接收。
    // 64 words = 9 complete commands plus one word; scheduler only consumes
    // complete seven-word records, while prog_full remains CPU backpressure.
    gpu_sync_fifo #(.WIDTH(32),.DEPTH(64),.PROG_FULL_LEVEL(60)) u_cmd_fifo(
        .clk(user_clk),.rst_n(rst_n),.wr_en(cmd_wr_en && (cmd_wr_strb==4'hf)),.wr_data(cmd_wr_data),
        .wr_keep(1'b1),
        .rd_en(cmd_rd_en),.rd_data(cmd_rd_data),.full(cmd_full),.prog_full(cmd_prog_full),.empty(cmd_empty),.count_used(cmd_word_count));
    genvar g;
    generate for (g=0; g<CORE_COUNT; g=g+1) begin: CORE
        // One complete 1280-pixel RGB565 line fits in a FIFO.  Keeping the
        // source and destination queues at the former full-line size avoids
        // repeated reader/writer turnarounds on the shared AXI port.
        gpu_sync_fifo #(.WIDTH(64),.WR_WIDTH(64),.RD_WIDTH(16),.DEPTH(2048),.PROG_FULL_LEVEL(1984)) src_fifo(.clk(user_clk),.rst_n(rst_n),.wr_en(fifo_wr_en[g]&&!fifo_full[g]),.wr_data(fifo_wr_data[g*64 +: 64]),.wr_keep(fifo_wr_keep[g*4 +: 4]),.rd_en(fifo_rd_en[g]),.rd_data(fifo_rd_data[g*16 +: 16]),.full(fifo_full[g]),.prog_full(fifo_prog_full[g]),.empty(fifo_empty[g]));
        gpu_sync_fifo #(.WIDTH(64),.WR_WIDTH(64),.RD_WIDTH(16),.DEPTH(2048),.PROG_FULL_LEVEL(1984)) dst_fifo(.clk(user_clk),.rst_n(rst_n),.wr_en(dst_fifo_wr_en[g]&&!dst_fifo_full[g]),.wr_data(dst_fifo_wr_data[g*64 +: 64]),.wr_keep(dst_fifo_wr_keep[g*4 +: 4]),.rd_en(dst_fifo_rd_en[g]),.rd_data(dst_fifo_rd_data[g*16 +: 16]),.full(dst_fifo_full[g]),.prog_full(dst_fifo_prog_full[g]),.empty(dst_fifo_empty[g]));
        gpu_sync_fifo #(.WIDTH(16),.DEPTH(32),.PROG_FULL_LEVEL(28)) scale_fifo(
            .clk(user_clk),.rst_n(rst_n),
            .wr_en((op[g*3 +: 3]==3'd4) && scale_fifo_wr_en[g]),
            .wr_data(scale_fifo_wr_data[g*16 +: 16]),
            .wr_keep(1'b1),
            .rd_en(scale_fifo_rd_en[g]),.rd_data(scale_fifo_data[g*16 +: 16]),
            .full(scale_fifo_full[g]),.prog_full(),.empty(scale_fifo_empty[g]));
        assign fifo_rd_en[g] = write_fifo_rd_en[g];
        assign dst_fifo_rd_en[g]=((op[g*3 +: 3]==3'd2)||(op[g*3 +: 3]==3'd3)||(op[g*3 +: 3]==3'd5))&&fifo_rd_en[g];
        assign write_fifo_empty[g]=fifo_empty[g]||(((op[g*3 +: 3]==3'd2)||(op[g*3 +: 3]==3'd3)||(op[g*3 +: 3]==3'd5))&&dst_fifo_empty[g]);
    end endgenerate
    // Parser registers the scaled source row coordinate before issuing each read.
    assign dma_read_addr = read_addr;
    gpu_schduler8 #(.CORE_COUNT(CORE_COUNT)) scheduler(.clk(user_clk),.rst_n(rst_n),.cmd_fifo_empty(cmd_empty),.cmd_fifo_word_count(cmd_word_count),.cmd_fifo_rd_en(cmd_rd_en),.cmd_fifo_rd_data(cmd_rd_data),.ctrl(sched_ctrl),.src(sched_src),.dst(sched_dst),.wh(sched_wh),.src_stride(sched_ss),.dst_stride(sched_ds),.color(sched_color),.parser_ready(~parser_busy),.parser_busy(parser_busy),.cache_access_done((read_done & cache_last_batch) | parser_done | parser_error),.parser_cmd_valid(sched_valid),.scheduler_busy(sched_busy));
    bitblt_cmd_parser8 #(.CORE_COUNT(CORE_COUNT),.ADDR_WIDTH(AXI_ADDR_WIDTH)) parsers(.clk(user_clk),.rst_n(rst_n),.ctrl(sched_ctrl),.src(sched_src),.dst(sched_dst),.wh(sched_wh),.src_stride(sched_ss),.dst_stride(sched_ds),.color(sched_color),.cmd_valid(sched_valid),.op(op),.color_out(color),.cache_control(cache_control),.cache_row(cache_row),.cache_last_batch(cache_last_batch),.scale_q26(scale_q26),.read_cmd_valid(read_valid),.read_cmd_ready(read_ready),.read_cmd_addr(read_addr),.read_cmd_length_bytes(read_len),.read_cmd_stride_bytes(read_stride),.read_cmd_rows(read_rows),.read_busy(read_busy),.read_done(read_done),.read_error(read_error),.dst_read_cmd_valid(dst_read_valid),.dst_read_cmd_ready(dst_read_ready),.dst_read_cmd_addr(dst_read_addr),.dst_read_cmd_length_bytes(dst_read_len),.dst_read_cmd_stride_bytes(dst_read_stride),.dst_read_cmd_rows(dst_read_rows),.dst_read_busy(dst_read_busy),.dst_read_done(dst_read_done),.dst_read_error(dst_read_error),.write_cmd_valid(write_valid),.write_cmd_ready(write_ready),.write_cmd_addr(write_addr),.write_cmd_length_bytes(write_len),.write_cmd_last(write_last),.write_busy(write_busy),.write_done(write_done),.write_error(write_error),.bitblt_busy(parser_busy),.bitblt_done(parser_done),.bitblt_error(parser_error));
    axi_rgb565_dma8 #(.CORE_COUNT(CORE_COUNT),.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),.AXI_ID_WIDTH(AXI_ID_WIDTH),.READ_BURST_LEN(READ_BURST_LEN),.WRITE_BURST_LEN(WRITE_BURST_LEN)) dma8(
        .clk(user_clk),.rst_n(rst_n),.op(op),.color(color),.cache_control(cache_control),.cache_row(cache_row),.cache_last_batch(cache_last_batch),.read_cmd_valid(read_valid),.read_cmd_ready(read_ready),.read_cmd_addr(dma_read_addr),.read_cmd_length_bytes(read_len),.read_cmd_stride_bytes(read_stride),.read_cmd_rows(read_rows),.read_busy(read_busy),.read_done(read_done),.read_error(read_error),.dst_read_cmd_valid(dst_read_valid),.dst_read_cmd_ready(dst_read_ready),.dst_read_cmd_addr(dst_read_addr),.dst_read_cmd_length_bytes(dst_read_len),.dst_read_cmd_stride_bytes(dst_read_stride),.dst_read_cmd_rows(dst_read_rows),.dst_read_busy(dst_read_busy),.dst_read_done(dst_read_done),.dst_read_error(dst_read_error),.read_pause_new_bursts(read_pause_new_bursts_q),.read_fifo_full(fifo_full),.read_fifo_wr_en(fifo_wr_en),.read_fifo_wr_data(fifo_wr_data),.read_fifo_wr_keep(fifo_wr_keep),.read_fifo_wr_last(fifo_wr_last),.dst_read_fifo_full(dst_fifo_full),.dst_read_fifo_wr_en(dst_fifo_wr_en),.dst_read_fifo_wr_data(dst_fifo_wr_data),.dst_read_fifo_wr_keep(dst_fifo_wr_keep),.write_cmd_valid(write_valid),.write_cmd_ready(write_ready),.write_cmd_addr(write_addr),.write_cmd_length_bytes(write_len),.write_cmd_last(write_last),.write_abort(parser_error),.write_busy(write_busy),.write_done(write_done),.write_error(write_error),.write_fifo_empty(write_fifo_empty),.write_fifo_rd_en(write_fifo_rd_en),.write_fifo_rd_data(fifo_rd_data),.scale_fifo_empty(scale_fifo_empty),.scale_fifo_full(scale_fifo_full),.scale_fifo_data(scale_fifo_data),.scale_fifo_rd_en(scale_fifo_rd_en),.scale_fifo_wr_en(scale_fifo_wr_en),.scale_fifo_wr_data(scale_fifo_wr_data),.dst_fifo_rd_data(dst_fifo_rd_data),.m_arid(io_ddrMasters_1_ar_payload_id),.m_araddr(io_ddrMasters_1_ar_payload_addr),.m_arlen(io_ddrMasters_1_ar_payload_len),.m_arsize(io_ddrMasters_1_ar_payload_size),.m_arburst(io_ddrMasters_1_ar_payload_burst),.m_arcache(io_ddrMasters_1_ar_payload_cache),.m_arqos(io_ddrMasters_1_ar_payload_qos),.m_arregion(io_ddrMasters_1_ar_payload_region),.m_arprot(io_ddrMasters_1_ar_payload_prot),.m_arlock(io_ddrMasters_1_ar_payload_lock),.m_arvalid(io_ddrMasters_1_ar_valid),.m_arready(io_ddrMasters_1_ar_ready),.m_rid(io_ddrMasters_1_r_payload_id),.m_rdata(io_ddrMasters_1_r_payload_data),.m_rresp(io_ddrMasters_1_r_payload_resp),.m_rlast(io_ddrMasters_1_r_payload_last),.m_rvalid(io_ddrMasters_1_r_valid),.m_rready(io_ddrMasters_1_r_ready),.m_awid(io_ddrMasters_1_aw_payload_id),.m_awaddr(io_ddrMasters_1_aw_payload_addr),.m_awlen(io_ddrMasters_1_aw_payload_len),.m_awsize(io_ddrMasters_1_aw_payload_size),.m_awburst(io_ddrMasters_1_aw_payload_burst),.m_awcache(io_ddrMasters_1_aw_payload_cache),.m_awqos(io_ddrMasters_1_aw_payload_qos),.m_awregion(io_ddrMasters_1_aw_payload_region),.m_awprot(io_ddrMasters_1_aw_payload_prot),.m_awlock(io_ddrMasters_1_aw_payload_lock),.m_awvalid(io_ddrMasters_1_aw_valid),.m_awready(io_ddrMasters_1_aw_ready),.m_wdata(io_ddrMasters_1_w_payload_data),.m_wstrb(io_ddrMasters_1_w_payload_strb),.m_wlast(io_ddrMasters_1_w_payload_last),.m_wvalid(io_ddrMasters_1_w_valid),.m_wready(io_ddrMasters_1_w_ready),.m_bid(io_ddrMasters_1_b_payload_id),.m_bresp(io_ddrMasters_1_b_payload_resp),.m_bvalid(io_ddrMasters_1_b_valid),.m_bready(io_ddrMasters_1_b_ready));

    axi_gpu_cmd_axi_slave #(.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),.AXI_DATA_WIDTH(32),.AXI_ID_WIDTH(USER_AXI_ID_WIDTH)) u_cmd_slave(
.s_axi_aclk(user_clk),.s_axi_aresetn(rst_n),.s_axi_awid(axiA_awid),.s_axi_awaddr(axiA_awaddr),.s_axi_awlen(axiA_awlen),.s_axi_awsize(axiA_awsize),.s_axi_awburst(axiA_awburst),.s_axi_awcache(axiA_awcache),.s_axi_awqos(axiA_awqos),.s_axi_awregion(axiA_awregion),.s_axi_awprot(axiA_awprot),.s_axi_awlock(axiA_awlock),.s_axi_awvalid(axiA_awvalid),.s_axi_awready(axiA_awready),.s_axi_wdata(axiA_wdata),.s_axi_wstrb(axiA_wstrb),.s_axi_wlast(axiA_wlast),.s_axi_wvalid(axiA_wvalid),.s_axi_wready(axiA_wready),.s_axi_bid(axiA_bid),.s_axi_bresp(axiA_bresp),.s_axi_bvalid(axiA_bvalid),.s_axi_bready(axiA_bready),.s_axi_arid(axiA_arid),.s_axi_araddr(axiA_araddr),.s_axi_arlen(axiA_arlen),.s_axi_arsize(axiA_arsize),.s_axi_arburst(axiA_arburst),.s_axi_arcache(axiA_arcache),.s_axi_arqos(axiA_arqos),.s_axi_arregion(axiA_arregion),.s_axi_arprot(axiA_arprot),.s_axi_arlock(axiA_arlock),.s_axi_arvalid(axiA_arvalid),.s_axi_arready(axiA_arready),.s_axi_rid(axiA_rid),.s_axi_rdata(axiA_rdata),.s_axi_rresp(axiA_rresp),.s_axi_rlast(axiA_rlast),.s_axi_rvalid(axiA_rvalid),.s_axi_rready(axiA_rready),.cmd_fifo_wr_en(cmd_wr_en),.cmd_fifo_wr_data(cmd_wr_data),.cmd_fifo_wr_strb(cmd_wr_strb),.cmd_fifo_full(cmd_full),.cmd_fifo_prog_full(cmd_prog_full),.cmd_fifo_empty(cmd_empty),.engine_busy((|parser_busy)||sched_busy),.engine_done(|parser_done),.engine_error(|parser_error));
endmodule
