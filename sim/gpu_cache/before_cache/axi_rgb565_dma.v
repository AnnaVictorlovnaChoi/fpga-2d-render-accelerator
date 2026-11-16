`timescale 1ns/1ps

// Shared DMA for the 8-core GPU variant.  The legacy two-channel
// module above is kept for the current top-level while the parser/scheduler
// migration is completed.  This block is intentionally self-contained so it
// can replace that instance without changing the reader/writer engines.
module axi_rgb565_dma8 #(
    parameter integer CORE_COUNT = 8,
    parameter integer AXI_ADDR_WIDTH = 32,
    parameter integer AXI_ID_WIDTH = 4,
    parameter integer READ_BURST_LEN = 8,
    parameter integer WRITE_BURST_LEN = 8
) (
    input wire clk, input wire rst_n,
    input wire [CORE_COUNT*3-1:0] op,
    input wire [CORE_COUNT*16-1:0] color,
    input wire [CORE_COUNT-1:0] read_cmd_valid,
    output wire [CORE_COUNT-1:0] read_cmd_ready,
    input wire [CORE_COUNT*AXI_ADDR_WIDTH-1:0] read_cmd_addr,
    input wire [CORE_COUNT*32-1:0] read_cmd_length_bytes,
    input wire [CORE_COUNT*32-1:0] read_cmd_stride_bytes,
    input wire [CORE_COUNT*7-1:0] read_cmd_rows,
    output wire [CORE_COUNT-1:0] read_busy,
    output wire [CORE_COUNT-1:0] read_done,
    output wire [CORE_COUNT-1:0] read_error,
    input wire [CORE_COUNT-1:0] dst_read_cmd_valid,
    output wire [CORE_COUNT-1:0] dst_read_cmd_ready,
    input wire [CORE_COUNT*AXI_ADDR_WIDTH-1:0] dst_read_cmd_addr,
    input wire [CORE_COUNT*32-1:0] dst_read_cmd_length_bytes,
    input wire [CORE_COUNT*32-1:0] dst_read_cmd_stride_bytes,
    input wire [CORE_COUNT*7-1:0] dst_read_cmd_rows,
    output wire [CORE_COUNT-1:0] dst_read_busy,
    output wire [CORE_COUNT-1:0] dst_read_done,
    output wire [CORE_COUNT-1:0] dst_read_error,
    input wire read_pause_new_bursts,
    input wire [CORE_COUNT-1:0] read_fifo_full,
    output wire [CORE_COUNT-1:0] read_fifo_wr_en,
    output wire [CORE_COUNT*64-1:0] read_fifo_wr_data,
    output wire [CORE_COUNT*4-1:0] read_fifo_wr_keep,
    output wire [CORE_COUNT-1:0] read_fifo_wr_last,
    input wire [CORE_COUNT-1:0] dst_read_fifo_full,
    output wire [CORE_COUNT-1:0] dst_read_fifo_wr_en,
    output wire [CORE_COUNT*64-1:0] dst_read_fifo_wr_data,
    output wire [CORE_COUNT*4-1:0] dst_read_fifo_wr_keep,
    input wire [CORE_COUNT-1:0] write_cmd_valid,
    output wire [CORE_COUNT-1:0] write_cmd_ready,
    input wire [CORE_COUNT*AXI_ADDR_WIDTH-1:0] write_cmd_addr,
    input wire [CORE_COUNT*32-1:0] write_cmd_length_bytes,
    output wire [CORE_COUNT-1:0] write_busy,
    output wire [CORE_COUNT-1:0] write_done,
    output wire [CORE_COUNT-1:0] write_error,
    input wire [CORE_COUNT-1:0] write_fifo_empty,
    output wire [CORE_COUNT-1:0] write_fifo_rd_en,
    input wire [CORE_COUNT*16-1:0] write_fifo_rd_data,
    input wire [CORE_COUNT-1:0] scale_fifo_empty,
    input wire [CORE_COUNT-1:0] scale_fifo_full,
    input wire [CORE_COUNT*16-1:0] scale_fifo_data,
    output wire [CORE_COUNT-1:0] scale_fifo_rd_en,
    output wire [CORE_COUNT-1:0] scale_fifo_wr_en,
    output wire [CORE_COUNT*16-1:0] scale_fifo_wr_data,
    input wire [CORE_COUNT*16-1:0] dst_fifo_rd_data,
    output wire [AXI_ID_WIDTH-1:0] m_arid,
    output wire [AXI_ADDR_WIDTH-1:0] m_araddr,
    output wire [7:0] m_arlen,
    output wire [2:0] m_arsize,
    output wire [1:0] m_arburst,
    output wire [3:0] m_arcache,
    output wire [3:0] m_arqos,
    output wire [3:0] m_arregion,
    output wire [2:0] m_arprot,
    output wire m_arlock, output wire m_arvalid, input wire m_arready,
    input wire [AXI_ID_WIDTH-1:0] m_rid,
    input wire [63:0] m_rdata, input wire [1:0] m_rresp,
    input wire m_rlast, input wire m_rvalid, output wire m_rready,
    output wire [AXI_ID_WIDTH-1:0] m_awid,
    output wire [AXI_ADDR_WIDTH-1:0] m_awaddr,
    output wire [7:0] m_awlen,
    output wire [2:0] m_awsize,
    output wire [1:0] m_awburst,
    output wire [3:0] m_awcache,
    output wire [3:0] m_awqos,
    output wire [3:0] m_awregion,
    output wire [2:0] m_awprot,
    output wire m_awlock, output wire m_awvalid, input wire m_awready,
    output wire [63:0] m_wdata, output wire [7:0] m_wstrb,
    output wire m_wlast, output wire m_wvalid, input wire m_wready,
    input wire [AXI_ID_WIDTH-1:0] m_bid, input wire [1:0] m_bresp,
    input wire m_bvalid, output wire m_bready
);
    // Read and write traffic use independent owners. AXI has independent
    // read/write channels, so a read burst no longer blocks a write burst.
    localparam integer READ_OWNER_W = $clog2(2*CORE_COUNT+1);
    localparam integer WRITE_OWNER_W = $clog2(CORE_COUNT+1);
    localparam integer READ_NONE = 0;
    localparam integer WRITE_NONE = 0;
    reg [READ_OWNER_W-1:0] read_owner;
    reg [WRITE_OWNER_W-1:0] write_owner;
    reg [3:0] write_gap_count;
    reg [CORE_COUNT-1:0] grant_r, grant_d, grant_w;
    wire [CORE_COUNT-1:0] rb, rd, re, db, dd, de, wb, wd, we;
    wire [CORE_COUNT-1:0] wcmd_ready, wbus_req, wburst_done;
    wire [CORE_COUNT-1:0] rav, rrr, dav, drr, wav, wwv, wbr;
    wire [CORE_COUNT-1:0] rbus_req, rbus_yield, dbus_req, dbus_yield;
    wire [AXI_ID_WIDTH-1:0] rai [0:CORE_COUNT-1];
    wire [AXI_ADDR_WIDTH-1:0] raa [0:CORE_COUNT-1];
    wire [7:0] ral [0:CORE_COUNT-1]; wire [2:0] ras [0:CORE_COUNT-1];
    wire [1:0] rab [0:CORE_COUNT-1]; wire [3:0] rac [0:CORE_COUNT-1];
    wire [3:0] raq [0:CORE_COUNT-1]; wire [3:0] rag [0:CORE_COUNT-1];
    wire [2:0] rap [0:CORE_COUNT-1]; wire rak [0:CORE_COUNT-1];
    wire [63:0] rfd [0:CORE_COUNT-1]; wire [3:0] rfk [0:CORE_COUNT-1];
    wire rfl [0:CORE_COUNT-1];
    wire [AXI_ID_WIDTH-1:0] dai [0:CORE_COUNT-1];
    wire [AXI_ADDR_WIDTH-1:0] daa [0:CORE_COUNT-1];
    wire [7:0] dal [0:CORE_COUNT-1]; wire [2:0] das [0:CORE_COUNT-1];
    wire [1:0] dab [0:CORE_COUNT-1]; wire [3:0] dac [0:CORE_COUNT-1];
    wire [3:0] daq [0:CORE_COUNT-1]; wire [3:0] dag [0:CORE_COUNT-1];
    wire [2:0] dap [0:CORE_COUNT-1]; wire dak [0:CORE_COUNT-1];
    wire [63:0] dfd [0:CORE_COUNT-1]; wire [3:0] dfk [0:CORE_COUNT-1];
    wire [AXI_ID_WIDTH-1:0] wai [0:CORE_COUNT-1];
    wire [AXI_ADDR_WIDTH-1:0] waa [0:CORE_COUNT-1];
    wire [7:0] wal [0:CORE_COUNT-1]; wire [2:0] was [0:CORE_COUNT-1];
    wire [1:0] wab [0:CORE_COUNT-1]; wire [3:0] wac [0:CORE_COUNT-1];
    wire [3:0] waq [0:CORE_COUNT-1]; wire [3:0] wag [0:CORE_COUNT-1];
    wire [2:0] wap [0:CORE_COUNT-1]; wire wak [0:CORE_COUNT-1];
    wire [63:0] wdata [0:CORE_COUNT-1]; wire [7:0] wstrb [0:CORE_COUNT-1];
    wire wlast [0:CORE_COUNT-1];
    wire [CORE_COUNT-1:0] rfe, dfe, wfr;
    // Two-stage write-data pipeline. Stage 0 isolates the FIFO RAM read from
    // the RGB565 mix arithmetic; stage 1 feeds the writer through ready/valid.
    function [4:0] blend5;
        input [4:0] src_value;
        input [4:0] dst_value;
        input [5:0] alpha_value;
        reg [11:0] blend_sum;
        begin
            blend_sum = src_value * alpha_value + dst_value * (6'd32 - alpha_value);
            blend5 = blend_sum >> 5;
        end
    endfunction
    function [5:0] blend6;
        input [5:0] src_value;
        input [5:0] dst_value;
        input [5:0] alpha_value;
        reg [11:0] blend_sum;
        begin
            blend_sum = src_value * alpha_value + dst_value * (6'd32 - alpha_value);
            blend6 = blend_sum >> 5;
        end
    endfunction
    genvar g;
    generate for (g=0; g<CORE_COUNT; g=g+1) begin: DMA_CORE
        wire [2:0] op_i = op[g*3 +: 3];
        wire [15:0] color_i = color[g*16 +: 16];
        // 缩放倍率沿用 parser 锁存的 color 低 8 位，避免改变顶层长端口连接。
        wire [7:0] scale_i = color_i[7:0];
        wire [AXI_ADDR_WIDTH-1:0] read_addr_i = read_cmd_addr[g*AXI_ADDR_WIDTH +: AXI_ADDR_WIDTH];
        wire [31:0] read_len_i = read_cmd_length_bytes[g*32 +: 32];
        wire [31:0] read_stride_i = read_cmd_stride_bytes[g*32 +: 32];
        wire [6:0] read_rows_i = read_cmd_rows[g*7 +: 7];
        wire [AXI_ADDR_WIDTH-1:0] dst_addr_i = dst_read_cmd_addr[g*AXI_ADDR_WIDTH +: AXI_ADDR_WIDTH];
        wire [31:0] dst_len_i = dst_read_cmd_length_bytes[g*32 +: 32];
        wire [31:0] dst_stride_i = dst_read_cmd_stride_bytes[g*32 +: 32];
        wire [6:0] dst_rows_i = dst_read_cmd_rows[g*7 +: 7];
        wire [AXI_ADDR_WIDTH-1:0] write_addr_i = write_cmd_addr[g*AXI_ADDR_WIDTH +: AXI_ADDR_WIDTH];
        wire [31:0] write_len_i = write_cmd_length_bytes[g*32 +: 32];
        reg pipe0_valid_i, pipe1_valid_i;
        reg [15:0] pipe0_src_i, pipe0_dst_i, pipe1_data_i;
        wire scale_src_pop_i;
        wire scale_valid_i;
        wire [15:0] scale_data_i;
        wire scale_line_done_i;
        wire [5:0] alpha_i = {1'b0,color_i[4:0]};
        wire [15:0] mixed_data_i = (op_i==3'd3) ?
            {blend5(pipe0_src_i[15:11],pipe0_dst_i[15:11],alpha_i),
             blend6(pipe0_src_i[10:5],pipe0_dst_i[10:5],alpha_i),
             blend5(pipe0_src_i[4:0],pipe0_dst_i[4:0],alpha_i)} :
             (op_i==3'd5&& pipe0_src_i==16'h0000)?pipe0_dst_i:
             (op_i==3'd5)?{blend5(pipe0_src_i[15:11],pipe0_dst_i[15:11],alpha_i),
             blend6(pipe0_src_i[10:5],pipe0_dst_i[10:5],alpha_i),
             blend5(pipe0_src_i[4:0],pipe0_dst_i[4:0],alpha_i)} :
            ((op_i==3'd2 && pipe0_src_i==16'h0000) ? pipe0_dst_i : pipe0_src_i);
        wire pipe1_ready_i = !pipe1_valid_i || wfr[g];
        wire pipe0_ready_i = !pipe0_valid_i || pipe1_ready_i;
        wire fifo_pop_i = (op_i != 3'd1) && !write_fifo_empty[g] && pipe0_ready_i &&
                          (op_i != 3'd4);
        gpu_scale_x_sampler scale_x_sampler(
            .clk(clk), .rst_n(rst_n), .enable(op_i == 3'd4),
            .line_start(write_cmd_valid[g] && write_cmd_ready[g]),
            .scale_q26(scale_i), .dst_width(write_len_i[16:1]),
            .src_fifo_empty(write_fifo_empty[g]),
            .src_fifo_data(write_fifo_rd_data[g*16 +: 16]),
            .src_fifo_rd_en(scale_src_pop_i), .out_valid(scale_valid_i),
            .out_ready(!scale_fifo_full[g]), .out_data(scale_data_i),
            .line_done(scale_line_done_i));
        axi_rgb565_reader #(.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),.AXI_ID_WIDTH(AXI_ID_WIDTH),.BURST_LEN(READ_BURST_LEN),.DESC_DEPTH(32)) reader(
            .clk(clk),.rst_n(rst_n),.cmd_valid(read_cmd_valid[g]&&grant_r[g]&&(op_i!=3'd1)),.cmd_ready(),.cmd_addr(read_addr_i),.cmd_length_bytes(read_len_i),.cmd_stride_bytes(read_stride_i),.cmd_rows(read_rows_i),.busy(rb[g]),.done(rd[g]),.error(re[g]),.bus_request(rbus_req[g]),.bus_yield(rbus_yield[g]),.pause_new_bursts(read_pause_new_bursts),.fifo_full(read_fifo_full[g]),.fifo_wr_en(rfe[g]),.fifo_wr_data(rfd[g]),.fifo_wr_keep(rfk[g]),.fifo_wr_last(rfl[g]),.m_axi_arid(rai[g]),.m_axi_araddr(raa[g]),.m_axi_arlen(ral[g]),.m_axi_arsize(ras[g]),.m_axi_arburst(rab[g]),.m_axi_arcache(rac[g]),.m_axi_arqos(raq[g]),.m_axi_arregion(rag[g]),.m_axi_arprot(rap[g]),.m_axi_arlock(rak[g]),.m_axi_arvalid(rav[g]),.m_axi_arready(m_arready&&(read_owner==g+1)),.m_axi_rid(m_rid),.m_axi_rdata(m_rdata),.m_axi_rresp(m_rresp),.m_axi_rlast(m_rlast),.m_axi_rvalid(m_rvalid&&(read_owner==g+1)),.m_axi_rready(rrr[g]));
        axi_rgb565_reader #(.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),.AXI_ID_WIDTH(AXI_ID_WIDTH),.BURST_LEN(READ_BURST_LEN),.DESC_DEPTH(32)) dst_reader(
            .clk(clk),.rst_n(rst_n),.cmd_valid(dst_read_cmd_valid[g]&&grant_d[g]&&((op_i==3'd2)||(op_i==3'd3)||(op_i==3'd5))),.cmd_ready(),.cmd_addr(dst_addr_i),.cmd_length_bytes(dst_len_i),.cmd_stride_bytes(dst_stride_i),.cmd_rows(dst_rows_i),.busy(db[g]),.done(dd[g]),.error(de[g]),.bus_request(dbus_req[g]),.bus_yield(dbus_yield[g]),.pause_new_bursts(read_pause_new_bursts),.fifo_full(dst_read_fifo_full[g]),.fifo_wr_en(dfe[g]),.fifo_wr_data(dfd[g]),.fifo_wr_keep(dfk[g]),.m_axi_arid(dai[g]),.m_axi_araddr(daa[g]),.m_axi_arlen(dal[g]),.m_axi_arsize(das[g]),.m_axi_arburst(dab[g]),.m_axi_arcache(dac[g]),.m_axi_arqos(daq[g]),.m_axi_arregion(dag[g]),.m_axi_arprot(dap[g]),.m_axi_arlock(dak[g]),.m_axi_arvalid(dav[g]),.m_axi_arready(m_arready&&(read_owner==CORE_COUNT+g+1)),.m_axi_rid(m_rid),.m_axi_rdata(m_rdata),.m_axi_rresp(m_rresp),.m_axi_rlast(m_rlast),.m_axi_rvalid(m_rvalid&&(read_owner==CORE_COUNT+g+1)),.m_axi_rready(drr[g]));
        axi_rgb565_writer #(.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),.AXI_ID_WIDTH(AXI_ID_WIDTH),.BURST_LEN(WRITE_BURST_LEN)) writer(
            .clk(clk),.rst_n(rst_n),.cmd_valid(write_cmd_valid[g]&&write_cmd_ready[g]),.cmd_ready(wcmd_ready[g]),.cmd_addr(write_addr_i),.cmd_length_bytes(write_len_i),.bus_request(wbus_req[g]),.bus_grant(write_owner==g+1),.burst_done(wburst_done[g]),.busy(wb[g]),.done(wd[g]),.error(we[g]),.fifo_valid((op_i==3'd1)||((op_i==3'd4)?!scale_fifo_empty[g]:pipe1_valid_i)),.fifo_ready(wfr[g]),.fifo_data((op_i==3'd1)?color_i:((op_i==3'd4)?scale_fifo_data[g*16 +: 16]:pipe1_data_i)),.m_axi_awid(wai[g]),.m_axi_awaddr(waa[g]),.m_axi_awlen(wal[g]),.m_axi_awsize(was[g]),.m_axi_awburst(wab[g]),.m_axi_awcache(wac[g]),.m_axi_awqos(waq[g]),.m_axi_awregion(wag[g]),.m_axi_awprot(wap[g]),.m_axi_awlock(wak[g]),.m_axi_awvalid(wav[g]),.m_axi_awready(m_awready&&(write_owner==g+1)),.m_axi_wdata(wdata[g]),.m_axi_wstrb(wstrb[g]),.m_axi_wlast(wlast[g]),.m_axi_wvalid(wwv[g]),.m_axi_wready(m_wready&&(write_owner==g+1)),.m_axi_bid(m_bid),.m_axi_bresp(m_bresp),.m_axi_bvalid(m_bvalid&&(write_owner==g+1)),.m_axi_bready(wbr[g]));
        assign read_busy[g]=rb[g]; assign read_done[g]=rd[g]; assign read_error[g]=re[g];
        assign dst_read_busy[g]=db[g]; assign dst_read_done[g]=dd[g]; assign dst_read_error[g]=de[g];
        assign write_busy[g]=wb[g]; assign write_done[g]=wd[g]; assign write_error[g]=we[g];
        assign read_fifo_wr_en[g]=rfe[g]; assign read_fifo_wr_data[g*64 +: 64]=rfd[g]; assign read_fifo_wr_keep[g*4 +: 4]=rfk[g]; assign read_fifo_wr_last[g]=rfl[g];
        assign dst_read_fifo_wr_en[g]=dfe[g]; assign dst_read_fifo_wr_data[g*64 +: 64]=dfd[g]; assign dst_read_fifo_wr_keep[g*4 +: 4]=dfk[g];
        assign scale_fifo_rd_en[g]=(op_i==3'd4) && wfr[g] && !scale_fifo_empty[g];
        assign scale_fifo_wr_en[g]=(op_i==3'd4) && scale_valid_i && !scale_fifo_full[g];
        assign scale_fifo_wr_data[g*16 +: 16]=scale_data_i;
        assign write_fifo_rd_en[g]=(op_i==3'd4)?scale_src_pop_i:fifo_pop_i;
        always @(posedge clk) begin
            if (!rst_n) begin
                pipe0_valid_i <= 1'b0;
                pipe1_valid_i <= 1'b0;
            end else if (op_i == 3'd1) begin
                pipe0_valid_i <= 1'b0;
                pipe1_valid_i <= 1'b0;
            end else begin
                if (pipe1_ready_i && op_i != 3'd4) begin
                    pipe1_valid_i <= pipe0_valid_i;
                    if (pipe0_valid_i)
                        pipe1_data_i <= mixed_data_i;
                end
                if (pipe0_ready_i) begin
                    pipe0_valid_i <= (op_i != 3'd4) && fifo_pop_i;
                    if (fifo_pop_i) begin
                        pipe0_src_i <= write_fifo_rd_data[g*16 +: 16];
                        pipe0_dst_i <= dst_fifo_rd_data[g*16 +: 16];
                    end
                end
            end
        end
    end endgenerate

    integer read_arb_i, write_arb_i, read_seq_i, write_seq_i;
    reg read_grant_taken;
    always @* begin
        grant_r={CORE_COUNT{1'b0}}; grant_d={CORE_COUNT{1'b0}};
        read_grant_taken=1'b0;
        if (read_owner==READ_NONE) begin
            for (read_arb_i=0;read_arb_i<CORE_COUNT;read_arb_i=read_arb_i+1) begin
                // A reader request remains present while it is paused between
                // bursts.  Do not re-grant it while the FIFO high-water mark
                // is asserted; this is what lets a writer drain the FIFO.
                if (!read_grant_taken && !read_pause_new_bursts &&
                    (read_cmd_valid[read_arb_i] || rbus_req[read_arb_i]) &&
                    op[read_arb_i*3 +: 3]!=3'd1) begin grant_r[read_arb_i]=1'b1; read_grant_taken=1'b1; end
            end
            for (read_arb_i=0;read_arb_i<CORE_COUNT;read_arb_i=read_arb_i+1) begin
                if (!read_grant_taken && !read_pause_new_bursts &&
                    (dst_read_cmd_valid[read_arb_i] || dbus_req[read_arb_i]) &&
                    (op[read_arb_i*3 +: 3]==3'd2 || op[read_arb_i*3 +: 3]==3'd3||op[read_arb_i*3 +: 3]==3'd5)) begin grant_d[read_arb_i]=1'b1; read_grant_taken=1'b1; end
            end
        end
    end
    always @* begin
        grant_w={CORE_COUNT{1'b0}};
        if (write_owner==WRITE_NONE) begin
            for (write_arb_i=0;write_arb_i<CORE_COUNT;write_arb_i=write_arb_i+1) begin
                if (!(|grant_w) && wbus_req[write_arb_i]) begin
                    grant_w[write_arb_i]=1'b1;
                end
            end
        end
    end
    generate for (g=0; g<CORE_COUNT; g=g+1) begin: READY
        // An active reader uses the same grant to resume its AXI burst, but
        // must not acknowledge a new parser command while already busy.
        assign read_cmd_ready[g]=(read_owner==READ_NONE)&&grant_r[g]&&!rb[g];
        assign dst_read_cmd_ready[g]=(read_owner==READ_NONE)&&(!( |grant_r))&&grant_d[g]&&!db[g];
        // The parser hands a row to its local writer independently of the
        // write owner. The writer requests ownership only when a burst is ready.
        assign write_cmd_ready[g]=wcmd_ready[g];
    end endgenerate

    always @(posedge clk) begin
        if (!rst_n) read_owner<=READ_NONE;
        else if (read_owner==READ_NONE) begin
            for (read_seq_i=0;read_seq_i<CORE_COUNT;read_seq_i=read_seq_i+1) begin
                if (grant_r[read_seq_i]) read_owner<=read_seq_i+1;
                else if (grant_d[read_seq_i]) read_owner<=CORE_COUNT+read_seq_i+1;
            end
        end else if (read_owner<=CORE_COUNT) begin
            if (rd[read_owner-1]||re[read_owner-1]||rbus_yield[read_owner-1])
                read_owner<=READ_NONE;
        end else if (dd[read_owner-CORE_COUNT-1]||de[read_owner-CORE_COUNT-1]||
                     dbus_yield[read_owner-CORE_COUNT-1]) read_owner<=READ_NONE;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            write_owner<=WRITE_NONE;
            write_gap_count<=4'd0;
        end
        else if (write_owner==WRITE_NONE) begin
            if (write_gap_count != 0)
                write_gap_count <= write_gap_count - 1'b1;
            else
                for (write_seq_i=0;write_seq_i<CORE_COUNT;write_seq_i=write_seq_i+1)
                    if (grant_w[write_seq_i]) write_owner<=write_seq_i+1;
        end else if (wburst_done[write_owner-1]||we[write_owner-1])
        begin
            write_owner<=WRITE_NONE;
            if (wburst_done[write_owner-1])
                write_gap_count<=4'd6;
        end
    end

    integer j;
    reg [AXI_ID_WIDTH-1:0] arid_q, awid_q; reg [AXI_ADDR_WIDTH-1:0] araddr_q, awaddr_q;
    reg [7:0] arlen_q, awlen_q; reg [2:0] arsize_q, awsize_q; reg [1:0] arburst_q, awburst_q;
    reg [3:0] arcache_q, arqos_q, arregion_q, awcache_q, awqos_q, awregion_q; reg [2:0] arprot_q, awprot_q; reg arlock_q, awlock_q;
    reg arvalid_q, awvalid_q, rready_q, wvalid_q, bready_q; reg [63:0] wdata_q; reg [7:0] wstrb_q; reg wlast_q;
    always @* begin
        arid_q={AXI_ID_WIDTH{1'b0}}; araddr_q={AXI_ADDR_WIDTH{1'b0}}; arlen_q=8'd0; arsize_q=3'd2; arburst_q=2'b01; arcache_q=4'b0011; arqos_q=4'd0; arregion_q=4'd0; arprot_q=3'd0; arlock_q=0; arvalid_q=0; rready_q=0;
        awid_q={AXI_ID_WIDTH{1'b0}}; awaddr_q={AXI_ADDR_WIDTH{1'b0}}; awlen_q=8'd0; awsize_q=3'd3; awburst_q=2'b01; awcache_q=4'b0011; awqos_q=4'd0; awregion_q=4'd0; awprot_q=3'd0; awlock_q=0; awvalid_q=0; wdata_q=64'd0; wstrb_q=8'd0; wlast_q=0; wvalid_q=0; bready_q=0;
        for (j=0;j<CORE_COUNT;j=j+1) begin
            if (read_owner==j+1) begin arid_q=rai[j]; araddr_q=raa[j]; arlen_q=ral[j]; arsize_q=ras[j]; arburst_q=rab[j]; arcache_q=rac[j]; arqos_q=raq[j]; arregion_q=rag[j]; arprot_q=rap[j]; arlock_q=rak[j]; arvalid_q=rav[j]; rready_q=rrr[j]; end
            if (read_owner==CORE_COUNT+j+1) begin arid_q=dai[j]; araddr_q=daa[j]; arlen_q=dal[j]; arsize_q=das[j]; arburst_q=dab[j]; arcache_q=dac[j]; arqos_q=daq[j]; arregion_q=dag[j]; arprot_q=dap[j]; arlock_q=dak[j]; arvalid_q=dav[j]; rready_q=drr[j]; end
            if (write_owner==j+1) begin awid_q=wai[j]; awaddr_q=waa[j]; awlen_q=wal[j]; awsize_q=was[j]; awburst_q=wab[j]; awcache_q=wac[j]; awqos_q=waq[j]; awregion_q=wag[j]; awprot_q=wap[j]; awlock_q=wak[j]; awvalid_q=wav[j]; wdata_q=wdata[j]; wstrb_q=wstrb[j]; wlast_q=wlast[j]; wvalid_q=wwv[j]; bready_q=wbr[j]; end
        end
    end
    assign m_arid=arid_q; assign m_araddr=araddr_q; assign m_arlen=arlen_q; assign m_arsize=arsize_q; assign m_arburst=arburst_q; assign m_arcache=arcache_q; assign m_arqos=arqos_q; assign m_arregion=arregion_q; assign m_arprot=arprot_q; assign m_arlock=arlock_q; assign m_arvalid=arvalid_q; assign m_rready=rready_q;
    assign m_awid=awid_q; assign m_awaddr=awaddr_q; assign m_awlen=awlen_q; assign m_awsize=awsize_q; assign m_awburst=awburst_q; assign m_awcache=awcache_q; assign m_awqos=awqos_q; assign m_awregion=awregion_q; assign m_awprot=awprot_q; assign m_awlock=awlock_q; assign m_awvalid=awvalid_q; assign m_wdata=wdata_q; assign m_wstrb=wstrb_q; assign m_wlast=wlast_q; assign m_wvalid=wvalid_q; assign m_bready=bready_q;
endmodule
