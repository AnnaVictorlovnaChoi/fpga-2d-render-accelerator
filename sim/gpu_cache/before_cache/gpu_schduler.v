`timescale 1ns/1ps

// 调度器采用贪心分配的方式，针对多个例化的bitblt核心，则采用了按照FCFS顺序调度指令，有空则分配
module gpu_schduler #(
    parameter integer WIDTH = 32,
    parameter integer DEPTH = 256,
    parameter integer PROG_FULL_LEVEL = DEPTH-4
) (
    input wire clk, input wire rst_n,
    input wire cmd_fifo_empty, output wire cmd_fifo_rd_en,
    input wire [31:0] cmd_fifo_rd_data,
    input wire [7:0] cmd_fifo_word_count,
    
    output reg [31:0] ctrl,
    output reg [31:0] src,
    output reg [31:0] dst,
    output reg [31:0] wh,
    output reg [31:0] src_stride,
    output reg [31:0] dst_stride,
    output reg [31:0] color,
    
    output reg select,//这个参数为0选择parser0，为1选择parsser1
    input parser0_ready,
    input parser0_busy,
    input parser1_ready,
    input parser1_busy,
    output reg parser0_cmd_valid,
    output reg parser1_cmd_valid,
    output wire scheduler_busy
);

    localparam [3:0] S_IDLE = 4'd0;
    localparam [3:0] S_SRC  = 4'd1;
    localparam [3:0] S_DST  = 4'd2;
    localparam [3:0] S_WH   = 4'd3;
    localparam [3:0] S_SS   = 4'd4;
    localparam [3:0] S_DS   = 4'd5;
    localparam [3:0] S_COL  = 4'd6;
    localparam [3:0] S_SEND = 4'd7;
    reg [3:0] state;
    reg target;
    // 覆盖参数收集、派发以及 parser 锁存前的一拍。
    assign scheduler_busy = (state != S_IDLE) || parser0_cmd_valid || parser1_cmd_valid;
    // cmd_valid 拉高后，parser 会在下一个时钟沿锁存命令并更新 busy。
    // 这段交接周期内先把目标 parser 视为占用，避免连续命令重复分配。
    wire parser0_free = parser0_ready && !parser0_busy && !parser0_cmd_valid;
    wire parser1_free = parser1_ready && !parser1_busy && !parser1_cmd_valid;

    // 只有真正需要取一个字时才读 FIFO，避免 FIFO 为空时状态机误推进。
    // A command is seven words. Do not consume a partial record.
    wire command_available = !cmd_fifo_empty && (cmd_fifo_word_count >= 4'd7);
    assign cmd_fifo_rd_en =
                            ((state == S_IDLE && command_available && (parser0_free || parser1_free)) ||
                             (state == S_SRC) || (state == S_DST) ||
                             (state == S_WH) || (state == S_SS) ||
                             (state == S_DS) || (state == S_COL));

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= S_IDLE;
            target <= 1'b0;
            select <= 1'b0;
            ctrl <= 0; src <= 0; dst <= 0; wh <= 0;
            src_stride <= 0; dst_stride <= 0; color <= 0;
            parser0_cmd_valid <= 1'b0;
            parser1_cmd_valid <= 1'b0;
        end else begin
            parser0_cmd_valid <= 1'b0;
            parser1_cmd_valid <= 1'b0;
            case (state)
                S_IDLE: begin
                    if (command_available && (parser0_free || parser1_free)) begin
                        target <= parser0_free ? 1'b0 : 1'b1;
                        select <= parser0_free ? 1'b0 : 1'b1;
                        ctrl <= cmd_fifo_rd_data;
                        state <= S_SRC;
                    end
                end
                S_SRC: if (!cmd_fifo_empty) begin src <= cmd_fifo_rd_data; state <= S_DST; end
                S_DST: if (!cmd_fifo_empty) begin dst <= cmd_fifo_rd_data; state <= S_WH; end
                S_WH: if (!cmd_fifo_empty) begin wh <= cmd_fifo_rd_data; state <= S_SS; end
                S_SS: if (!cmd_fifo_empty) begin src_stride <= cmd_fifo_rd_data; state <= S_DS; end
                S_DS: if (!cmd_fifo_empty) begin dst_stride <= cmd_fifo_rd_data; state <= S_COL; end
                S_COL: if (!cmd_fifo_empty) begin color <= cmd_fifo_rd_data; state <= S_SEND; end
                S_SEND: begin
                    if (!target) parser0_cmd_valid <= 1'b1;
                    else parser1_cmd_valid <= 1'b1;
                    state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase
        end
    end

endmodule

// 8-way FCFS scheduler.  The command data bus is shared, but only one
// cmd_valid bit is asserted at a time; the selected parser must keep ready
// low/high independently while the scheduler holds the complete command.
module gpu_schduler8 #(
    parameter integer CORE_COUNT = 8
) (
    input wire clk, input wire rst_n,
    input wire cmd_fifo_empty,
    input wire [7:0] cmd_fifo_word_count,
    output wire cmd_fifo_rd_en,
    input wire [31:0] cmd_fifo_rd_data,
    output reg [31:0] ctrl, output reg [31:0] src,
    output reg [31:0] dst, output reg [31:0] wh,
    output reg [31:0] src_stride, output reg [31:0] dst_stride,
    output reg [31:0] color,
    input wire [CORE_COUNT-1:0] parser_ready,
    input wire [CORE_COUNT-1:0] parser_busy,
    output reg [CORE_COUNT-1:0] parser_cmd_valid,
    output wire scheduler_busy
);
    localparam [3:0] S_IDLE=4'd0, S_SRC=4'd1, S_DST=4'd2,
                     S_WH=4'd3, S_SS=4'd4, S_DS=4'd5,
                     S_COL=4'd6, S_SEND=4'd7;
    reg [3:0] state;
    reg [3:0] target;
    reg target_valid;
    wire command_available = !cmd_fifo_empty && (cmd_fifo_word_count >= 8'd7);
    wire any_parser_free;
    wire [CORE_COUNT-1:0] parser_free;
    genvar p;
    generate for (p=0; p<CORE_COUNT; p=p+1) begin: FREE
        assign parser_free[p] = parser_ready[p] && !parser_busy[p];
    end endgenerate
    assign any_parser_free = |parser_free;
    wire selected_ready = target_valid && parser_ready[target] && !parser_busy[target];

    assign cmd_fifo_rd_en = (state == S_IDLE) ?
                            (command_available && any_parser_free) :
                            (state != S_SEND);
    assign scheduler_busy = (state != S_IDLE) || target_valid || (|parser_busy);

    integer i;
    reg found;
    always @(posedge clk) begin
        if (!rst_n) begin
            state <= S_IDLE;
            target <= 4'd0;
            target_valid <= 1'b0;
            parser_cmd_valid <= {CORE_COUNT{1'b0}};
            ctrl <= 0; src <= 0; dst <= 0; wh <= 0;
            src_stride <= 0; dst_stride <= 0; color <= 0;
        end else begin
            // cmd_valid is a one-cycle acceptance pulse.  Command fields
            // remain latched on the parser side after this pulse.
            parser_cmd_valid <= {CORE_COUNT{1'b0}};
            case (state)
                S_IDLE: begin
                    target_valid <= 1'b0;
                    if (command_available && any_parser_free) begin
                        ctrl <= cmd_fifo_rd_data;
                        target_valid <= 1'b1;
                        target <= 4'd0;
                        found = 1'b0;
                        for (i=0; i<CORE_COUNT; i=i+1)
                            if (parser_free[i] && !found) begin
                                target <= i[3:0];
                                found = 1'b1;
                            end
                        state <= S_SRC;
                    end
                end
                S_SRC: begin if (!cmd_fifo_empty) begin src <= cmd_fifo_rd_data; state <= S_DST; end end
                S_DST: begin if (!cmd_fifo_empty) begin dst <= cmd_fifo_rd_data; state <= S_WH; end end
                S_WH:  begin if (!cmd_fifo_empty) begin wh <= cmd_fifo_rd_data; state <= S_SS; end end
                S_SS:  begin if (!cmd_fifo_empty) begin src_stride <= cmd_fifo_rd_data; state <= S_DS; end end
                S_DS:  begin if (!cmd_fifo_empty) begin dst_stride <= cmd_fifo_rd_data; state <= S_COL; end end
                S_COL: begin if (!cmd_fifo_empty) begin color <= cmd_fifo_rd_data; state <= S_SEND; end end
                S_SEND: begin
                    // The parser has no separate ready handshake: its
                    // idle/busy state is the acceptance indication.  Emit
                    // one valid pulse while it is idle, then immediately
                    // return to arbitration.  Holding valid until the
                    // parser becomes idle would re-submit the same command
                    // on the cycle in which the parser finishes.
                    if (selected_ready) begin
                        parser_cmd_valid[target] <= 1'b1;
                        target_valid <= 1'b0;
                        state <= S_IDLE;
                    end
                end
                default: begin state <= S_IDLE; target_valid <= 1'b0; end
            endcase
        end
    end
endmodule
