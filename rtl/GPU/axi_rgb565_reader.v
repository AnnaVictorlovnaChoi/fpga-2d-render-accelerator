`timescale 1ns/1ps

// RGB565 AXI reader. A command describes a batch of strided rows. A small
// ordered descriptor queue allows multiple same-ID bursts to be outstanding.
module axi_rgb565_reader #(
    parameter integer AXI_ADDR_WIDTH = 32,
    parameter integer AXI_ID_WIDTH = 4,
    parameter integer BURST_LEN = 8,
    parameter integer DESC_DEPTH = 32
) (
    input wire clk, input wire rst_n,
    input wire cmd_valid, output wire cmd_ready,
    input wire [AXI_ADDR_WIDTH-1:0] cmd_addr,
    input wire [31:0] cmd_length_bytes,
    input wire [31:0] cmd_stride_bytes,
    input wire [6:0] cmd_rows,
    output wire busy, output wire done, output wire error,
    output wire bus_request, output wire bus_yield,
    input wire pause_new_bursts, input wire fifo_full,
    output wire fifo_wr_en, output wire [63:0] fifo_wr_data,
    output wire [3:0] fifo_wr_keep, output wire fifo_wr_last,
    output wire [AXI_ID_WIDTH-1:0] m_axi_arid,
    output wire [AXI_ADDR_WIDTH-1:0] m_axi_araddr,
    output wire [7:0] m_axi_arlen, output wire [2:0] m_axi_arsize,
    output wire [1:0] m_axi_arburst, output wire [3:0] m_axi_arcache,
    output wire [3:0] m_axi_arqos, output wire [3:0] m_axi_arregion,
    output wire [2:0] m_axi_arprot, output wire m_axi_arlock,
    output wire m_axi_arvalid, input wire m_axi_arready,
    input wire [AXI_ID_WIDTH-1:0] m_axi_rid,
    input wire [63:0] m_axi_rdata, input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast, input wire m_axi_rvalid,
    output wire m_axi_rready
);
    localparam [1:0] ST_IDLE=2'd0, ST_AR=2'd1, ST_R=2'd2, ST_FINISH=2'd3;
    reg [1:0] state;

    localparam integer DESC_PTR_W = (DESC_DEPTH <= 2) ? 1 : $clog2(DESC_DEPTH);

    // Issue cursor advances on AR acceptance, independently of the active R
    // burst. Pending descriptors are consumed in AR order as R bursts finish.
    reg [AXI_ADDR_WIDTH-1:0] issue_row_addr, issue_addr;
    reg [31:0] issue_row_bytes, issue_row_bytes_left, issue_stride;
    reg [6:0] issue_rows_left;

    reg [8:0] active_beats_left;
    reg [31:0] active_bytes_left;
    reg [2:0] active_skip;
    reg active_row_last, active_cmd_last;

    reg [8:0] desc_beats [0:DESC_DEPTH-1];
    reg [31:0] desc_bytes [0:DESC_DEPTH-1];
    reg [2:0] desc_skip [0:DESC_DEPTH-1];
    reg desc_row_last [0:DESC_DEPTH-1];
    reg desc_cmd_last [0:DESC_DEPTH-1];
    reg [DESC_PTR_W-1:0] desc_wr_ptr, desc_rd_ptr;
    reg [DESC_PTR_W:0] desc_count;

    reg done_reg, error_reg;
    reg candidate_valid;
    wire [8:0] candidate_beats_calc;
    wire [31:0] candidate_bytes_calc;
    wire [2:0] candidate_skip_calc = issue_addr[2:0];
    wire candidate_row_last_calc = (issue_row_bytes_left == candidate_bytes_calc);
    wire candidate_cmd_last_calc = candidate_row_last_calc && (issue_rows_left == 1);
    reg [AXI_ADDR_WIDTH-1:0] candidate_addr;
    reg [8:0] candidate_beats;
    reg [31:0] candidate_bytes;
    reg [2:0] candidate_skip;
    reg candidate_row_last, candidate_cmd_last;
    wire ar_fire = m_axi_arvalid && m_axi_arready;
    wire r_fire = m_axi_rvalid && m_axi_rready;
    wire active_last_fire = r_fire && (active_beats_left == 1);
    wire desc_pop = active_last_fire && !active_cmd_last && (desc_count != 0);
    wire desc_bypass = active_last_fire && !active_cmd_last &&
                       (desc_count == 0) && ar_fire;
    // The AR accepted in ST_AR becomes the active burst immediately; it must
    // not also be queued as a future burst. Only ARs issued while an active R
    // burst is running are descriptors for a later response burst.
    wire desc_push = (state == ST_R) && ar_fire && !desc_bypass;
    wire [3:0] beat_capacity = 4'd8 - {1'b0,active_skip};
    wire [3:0] beat_bytes = (active_bytes_left < beat_capacity) ?
                            active_bytes_left[3:0] : beat_capacity;
    wire beat_valid = (beat_bytes >= 3'd2);

    function [8:0] calc_burst_beats;
        input [AXI_ADDR_WIDTH-1:0] addr;
        input [31:0] bytes_left;
        reg [12:0] boundary_bytes;
        reg [10:0] boundary_beats;
        reg [31:0] needed, result;
        begin
            boundary_bytes = 13'd4096 - {1'b0,addr[11:0]};
            boundary_beats = boundary_bytes >> 3;
            needed = ({29'd0,addr[2:0]} + bytes_left + 7) >> 3;
            result = needed;
            if (result > BURST_LEN) result = BURST_LEN;
            if (result > boundary_beats) result = boundary_beats;
            if (result == 0) result = 1;
            calc_burst_beats = result[8:0];
        end
    endfunction

    function [31:0] calc_burst_bytes;
        input [AXI_ADDR_WIDTH-1:0] addr;
        input [31:0] bytes_left;
        input [8:0] beats;
        reg [31:0] capacity;
        begin
            capacity = ({23'd0,beats} << 3) - {29'd0,addr[2:0]};
            calc_burst_bytes = (bytes_left < capacity) ? bytes_left : capacity;
        end
    endfunction

    assign candidate_beats_calc = calc_burst_beats(issue_addr,issue_row_bytes_left);
    assign candidate_bytes_calc = calc_burst_bytes(issue_addr,issue_row_bytes_left,
                                                    candidate_beats_calc);
    assign cmd_ready = (state == ST_IDLE);
    assign busy = (state != ST_IDLE);
    assign done = done_reg;
    assign error = error_reg;
    assign bus_request = (state == ST_AR) || (state == ST_R);
    assign bus_yield = (state == ST_AR) && pause_new_bursts;
    assign m_axi_rready = (state == ST_R) && !fifo_full;
    assign fifo_wr_en = r_fire && beat_valid;
    assign fifo_wr_data = m_axi_rdata >> {active_skip,3'b000};
    assign fifo_wr_keep =
        {(beat_bytes >= 8), (beat_bytes >= 6),
         (beat_bytes >= 4), (beat_bytes >= 2)};
    assign fifo_wr_last = r_fire && beat_valid && active_cmd_last &&
                          (active_beats_left == 1);

    assign m_axi_arid = {AXI_ID_WIDTH{1'b0}};
    assign m_axi_araddr = {candidate_addr[AXI_ADDR_WIDTH-1:3],3'b000};
    assign m_axi_arlen = candidate_beats - 1'b1;
    assign m_axi_arsize = 3'd3;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arregion = 4'b0000;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arlock = 1'b0;
    // The SoC AXI upsizer accepts multiple commands with the same ID. Keep
    // several following bursts queued while the current burst returns data.
    assign m_axi_arvalid = ((state == ST_AR) ||
                            ((state == ST_R) && (issue_rows_left != 0) &&
                             (desc_count < DESC_DEPTH))) && candidate_valid &&
                           !pause_new_bursts;

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= ST_IDLE;
            issue_row_addr <= 0;
            issue_addr <= 0;
            issue_row_bytes <= 0;
            issue_row_bytes_left <= 0;
            issue_stride <= 0;
            issue_rows_left <= 0;
            active_beats_left <= 0;
            active_bytes_left <= 0;
            active_skip <= 0;
            active_row_last <= 0;
            active_cmd_last <= 0;
            desc_wr_ptr <= 0;
            desc_rd_ptr <= 0;
            desc_count <= 0;
            candidate_valid <= 0;
            candidate_addr <= 0;
            candidate_beats <= 0;
            candidate_bytes <= 0;
            candidate_skip <= 0;
            candidate_row_last <= 0;
            candidate_cmd_last <= 0;
            done_reg <= 0;
            error_reg <= 0;
        end else begin
            done_reg <= 1'b0;
            // Build a stable AR descriptor while the current R burst is in
            // flight. The descriptor is held until the AXI handshake.
            if (!candidate_valid && issue_rows_left != 0 &&
                (state == ST_AR || state == ST_R)) begin
                candidate_addr <= issue_addr;
                candidate_beats <= candidate_beats_calc;
                candidate_bytes <= candidate_bytes_calc;
                candidate_skip <= candidate_skip_calc;
                candidate_row_last <= candidate_row_last_calc;
                candidate_cmd_last <= candidate_cmd_last_calc;
                candidate_valid <= 1'b1;
            end
            if (desc_push) begin
                desc_beats[desc_wr_ptr] <= candidate_beats;
                desc_bytes[desc_wr_ptr] <= candidate_bytes;
                desc_skip[desc_wr_ptr] <= candidate_skip;
                desc_row_last[desc_wr_ptr] <= candidate_row_last;
                desc_cmd_last[desc_wr_ptr] <= candidate_cmd_last;
                desc_wr_ptr <= desc_wr_ptr + 1'b1;
            end
            if (desc_pop)
                desc_rd_ptr <= desc_rd_ptr + 1'b1;
            case ({desc_push, desc_pop})
                2'b10: desc_count <= desc_count + 1'b1;
                2'b01: desc_count <= desc_count - 1'b1;
                default: desc_count <= desc_count;
            endcase
            case (state)
                ST_IDLE: if (cmd_valid) begin
                    error_reg <= 1'b0;
                    if (cmd_length_bytes == 0 || cmd_rows == 0) begin
                        done_reg <= 1'b1;
                        candidate_valid <= 1'b0;
                    end else begin
                        issue_row_addr <= cmd_addr;
                        issue_addr <= cmd_addr;
                        issue_row_bytes <= cmd_length_bytes;
                        issue_row_bytes_left <= cmd_length_bytes;
                        issue_stride <= cmd_stride_bytes;
                        issue_rows_left <= cmd_rows;
                        candidate_valid <= 1'b0;
                        desc_wr_ptr <= 0;
                        desc_rd_ptr <= 0;
                        desc_count <= 0;
                        state <= ST_AR;
                    end
                end
                ST_AR: if (ar_fire) begin
                    active_beats_left <= candidate_beats;
                    active_bytes_left <= candidate_bytes;
                    active_skip <= candidate_skip;
                    active_row_last <= candidate_row_last;
                    active_cmd_last <= candidate_cmd_last;
                    // Rebuild the next descriptor on the following clock.
                    // Keeping the 4-KB/row calculation out of this AR-fire
                    // register path restores core-clock timing.
                    candidate_valid <= 1'b0;
                    if (candidate_row_last) begin
                        issue_rows_left <= issue_rows_left - 1'b1;
                        issue_row_addr <= issue_row_addr + issue_stride;
                        issue_addr <= issue_row_addr + issue_stride;
                        issue_row_bytes_left <= (issue_rows_left == 1) ? 0 :
                                                issue_row_bytes;
                    end else begin
                        issue_addr <= issue_addr + candidate_bytes;
                        issue_row_bytes_left <= issue_row_bytes_left - candidate_bytes;
                    end
                    state <= ST_R;
                end
                ST_R: begin
                    if (ar_fire) begin
                        // The candidate-build block above will calculate the
                        // next descriptor from the updated issue cursor.
                        candidate_valid <= 1'b0;
                        if (candidate_row_last) begin
                            issue_rows_left <= issue_rows_left - 1'b1;
                            issue_row_addr <= issue_row_addr + issue_stride;
                            issue_addr <= issue_row_addr + issue_stride;
                            issue_row_bytes_left <= (issue_rows_left == 1) ? 0 :
                                                    issue_row_bytes;
                        end else begin
                            issue_addr <= issue_addr + candidate_bytes;
                            issue_row_bytes_left <= issue_row_bytes_left - candidate_bytes;
                        end
                    end
                    if (r_fire) begin
                        if (m_axi_rresp != 2'b00)
                            error_reg <= 1'b1;
                        if (m_axi_rlast != (active_beats_left == 1))
                            error_reg <= 1'b1;
                        if (beat_bytes == 1)
                            error_reg <= 1'b1;
                        active_bytes_left <= active_bytes_left - beat_bytes;
                        active_beats_left <= active_beats_left - 1'b1;
                        active_skip <= 3'd0;
                        if (active_beats_left == 1) begin
                            if (active_cmd_last) begin
                                state <= ST_FINISH;
                            end else if (desc_count != 0) begin
                                active_beats_left <= desc_beats[desc_rd_ptr];
                                active_bytes_left <= desc_bytes[desc_rd_ptr];
                                active_skip <= desc_skip[desc_rd_ptr];
                                active_row_last <= desc_row_last[desc_rd_ptr];
                                active_cmd_last <= desc_cmd_last[desc_rd_ptr];
                            end else if (ar_fire) begin
                                active_beats_left <= candidate_beats;
                                active_bytes_left <= candidate_bytes;
                                active_skip <= candidate_skip;
                                active_row_last <= candidate_row_last;
                                active_cmd_last <= candidate_cmd_last;
                            end else begin
                                state <= ST_AR;
                            end
                        end
                    end
                end
                ST_FINISH: begin
                    done_reg <= 1'b1;
                    state <= ST_IDLE;
                end
                default: state <= ST_IDLE;
            endcase
        end
    end

    wire unused_rid = ^m_axi_rid;
endmodule
