`timescale 1ns/1ps

// AXI4-Full slave for CPU -> GPU command/status access.
// Intended for the SoC axiA master (32-bit data, 8-bit ID).
// Only single-beat accesses are needed for the command register; burst
// accesses are completed with DECERR and never enter the command FIFO.
//
// Register map:
//   0x00 write: enqueue WDATA into cmd_fifo (WSTRB is forwarded).
//   0x00 read : {26'd0, error, done, busy, empty, prog_full, full}.
//   0x04 read : {29'd0, empty, prog_full, full}.
module axi_gpu_cmd_axi_slave #(
    parameter integer AXI_ADDR_WIDTH = 32,
    parameter integer AXI_DATA_WIDTH = 32,
    parameter integer AXI_ID_WIDTH   = 8
) (
    input  wire                         s_axi_aclk,
    input  wire                         s_axi_aresetn,

    input  wire [AXI_ID_WIDTH-1:0]      s_axi_awid,
    input  wire [AXI_ADDR_WIDTH-1:0]    s_axi_awaddr,
    input  wire [7:0]                   s_axi_awlen,
    input  wire [2:0]                   s_axi_awsize,
    input  wire [1:0]                   s_axi_awburst,
    input  wire [3:0]                   s_axi_awcache,
    input  wire [3:0]                   s_axi_awqos,
    input  wire [3:0]                   s_axi_awregion,
    input  wire [2:0]                   s_axi_awprot,
    input  wire                         s_axi_awlock,
    input  wire                         s_axi_awvalid,
    output wire                         s_axi_awready,

    input  wire [AXI_DATA_WIDTH-1:0]    s_axi_wdata,
    input  wire [(AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                         s_axi_wlast,
    input  wire                         s_axi_wvalid,
    output wire                         s_axi_wready,

    output wire [AXI_ID_WIDTH-1:0]      s_axi_bid,
    output wire [1:0]                   s_axi_bresp,
    output wire                         s_axi_bvalid,
    input  wire                         s_axi_bready,

    input  wire [AXI_ID_WIDTH-1:0]      s_axi_arid,
    input  wire [AXI_ADDR_WIDTH-1:0]    s_axi_araddr,
    input  wire [7:0]                   s_axi_arlen,
    input  wire [2:0]                   s_axi_arsize,
    input  wire [1:0]                   s_axi_arburst,
    input  wire [3:0]                   s_axi_arcache,
    input  wire [3:0]                   s_axi_arqos,
    input  wire [3:0]                   s_axi_arregion,
    input  wire [2:0]                   s_axi_arprot,
    input  wire                         s_axi_arlock,
    input  wire                         s_axi_arvalid,
    output wire                         s_axi_arready,

    output wire [AXI_ID_WIDTH-1:0]      s_axi_rid,
    output wire [AXI_DATA_WIDTH-1:0]    s_axi_rdata,
    output wire [1:0]                   s_axi_rresp,
    output wire                         s_axi_rlast,
    output wire                         s_axi_rvalid,
    input  wire                         s_axi_rready,

    output wire                         cmd_fifo_wr_en,
    output wire [AXI_DATA_WIDTH-1:0]    cmd_fifo_wr_data,
    output wire [(AXI_DATA_WIDTH/8)-1:0] cmd_fifo_wr_strb,
    input  wire                         cmd_fifo_full,
    input  wire                         cmd_fifo_prog_full,
    input  wire                         cmd_fifo_empty,
    input  wire                         engine_busy,
    input  wire                         engine_done,
    input  wire                         engine_error
);

    localparam [1:0] RESP_OKAY = 2'b00;
    localparam [1:0] RESP_DECERR = 2'b11;

    reg aw_valid_q, w_valid_q;
    reg [AXI_ID_WIDTH-1:0] awid_q;
    reg [AXI_ADDR_WIDTH-1:0] awaddr_q;
    reg [7:0] awlen_q;
    reg [AXI_DATA_WIDTH-1:0] wdata_q;
    reg [(AXI_DATA_WIDTH/8)-1:0] wstrb_q;
    reg wlast_q;

    reg bvalid_q;
    reg [AXI_ID_WIDTH-1:0] bid_q;
    reg [1:0] bresp_q;
    reg rvalid_q;
    reg [AXI_ID_WIDTH-1:0] rid_q;
    reg [AXI_DATA_WIDTH-1:0] rdata_q;
    reg [1:0] rresp_q;
    reg done_q, error_q, engine_error_d;
    reg command_pending_q;

    // 命令 FIFO 以单个 32 位字接收，但一条指令由 7 个字组成。
    // 不能用 prog_full 作为写通道反压，否则可能在一条指令中途截断。
    wire fifo_busy = cmd_fifo_full;
    // busy 只表示命令 FIFO 没有空间；引擎运行期间允许继续排队。
    wire status_busy = fifo_busy;
    wire is_cmd_write = (awaddr_q[5:0] == 6'h00);
    wire write_complete = aw_valid_q && w_valid_q && !bvalid_q &&
                          (!is_cmd_write || !fifo_busy);
    wire write_legal = (awlen_q == 8'd0) && wlast_q;
    wire read_fire = s_axi_arvalid && s_axi_arready;

    assign s_axi_awready = !aw_valid_q && !bvalid_q && !fifo_busy;
    assign s_axi_wready  = !w_valid_q  && !bvalid_q && !fifo_busy;
    assign s_axi_bvalid = bvalid_q;
    assign s_axi_bid    = bid_q;
    assign s_axi_bresp  = bresp_q;

    assign s_axi_arready = !rvalid_q;
    assign s_axi_rvalid  = rvalid_q;
    assign s_axi_rid     = rid_q;
    assign s_axi_rdata   = rdata_q;
    assign s_axi_rresp   = rresp_q;
    assign s_axi_rlast   = rvalid_q;

    assign cmd_fifo_wr_en   = write_complete && write_legal;
    assign cmd_fifo_wr_data = wdata_q;
    assign cmd_fifo_wr_strb = wstrb_q;

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            aw_valid_q <= 1'b0;
            w_valid_q  <= 1'b0;
            awid_q     <= {AXI_ID_WIDTH{1'b0}};
            awaddr_q   <= {AXI_ADDR_WIDTH{1'b0}};
            awlen_q    <= 8'd0;
            wdata_q    <= {AXI_DATA_WIDTH{1'b0}};
            wstrb_q    <= {(AXI_DATA_WIDTH/8){1'b0}};
            wlast_q    <= 1'b0;
            bvalid_q   <= 1'b0;
            bid_q      <= {AXI_ID_WIDTH{1'b0}};
            bresp_q    <= RESP_OKAY;
            rvalid_q   <= 1'b0;
            rid_q      <= {AXI_ID_WIDTH{1'b0}};
            rdata_q    <= {AXI_DATA_WIDTH{1'b0}};
            rresp_q    <= RESP_OKAY;
            done_q     <= 1'b0;
            error_q    <= 1'b0;
            engine_error_d <= 1'b0;
            command_pending_q <= 1'b0;
        end
        else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_valid_q <= 1'b1;
                awid_q     <= s_axi_awid;
                awaddr_q   <= s_axi_awaddr;
                awlen_q    <= s_axi_awlen;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_valid_q <= 1'b1;
                wdata_q   <= s_axi_wdata;
                wstrb_q   <= s_axi_wstrb;
                wlast_q   <= s_axi_wlast;
            end

            if (write_complete) begin
                aw_valid_q <= 1'b0;
                w_valid_q  <= 1'b0;
                bvalid_q   <= 1'b1;
                bid_q      <= awid_q;
                bresp_q    <= write_legal ? RESP_OKAY : RESP_DECERR;
            end
            else if (bvalid_q && s_axi_bready) begin
                bvalid_q <= 1'b0;
            end

            if (cmd_fifo_wr_en) begin
                done_q  <= 1'b0;
                error_q <= 1'b0;
                command_pending_q <= 1'b1;
            end
            else if (command_pending_q && !engine_busy && cmd_fifo_empty) begin
                done_q <= 1'b1;
                command_pending_q <= 1'b0;
            end
            engine_error_d <= engine_error;
            if (engine_error && !engine_error_d)
                error_q <= 1'b1;

            if (read_fire) begin
                rvalid_q <= 1'b1;
                rid_q    <= s_axi_arid;
                rresp_q  <= (s_axi_arlen == 8'd0) ? RESP_OKAY : RESP_DECERR;
                case (s_axi_araddr[5:0])
                    6'h00: rdata_q <= {{(AXI_DATA_WIDTH-6){1'b0}},
                                         error_q, done_q, status_busy,
                                         cmd_fifo_empty, cmd_fifo_prog_full,
                                         cmd_fifo_full};
                    6'h04: rdata_q <= {{(AXI_DATA_WIDTH-3){1'b0}},
                                         cmd_fifo_empty, cmd_fifo_prog_full,
                                         cmd_fifo_full};
                    default: begin
                        rdata_q <= {AXI_DATA_WIDTH{1'b0}};
                        rresp_q <= RESP_DECERR;
                    end
                endcase
            end
            else if (rvalid_q && s_axi_rready) begin
                rvalid_q <= 1'b0;
            end
        end
    end
endmodule
