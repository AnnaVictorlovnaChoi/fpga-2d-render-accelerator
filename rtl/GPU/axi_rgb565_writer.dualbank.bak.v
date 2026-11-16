`timescale 1ns/1ps

// RGB565 AXI writer with two burst banks. One bank may be transmitted while
// the other bank is filled from the pixel stream.
module axi_rgb565_writer #(
    parameter integer AXI_ADDR_WIDTH = 32,
    parameter integer AXI_ID_WIDTH   = 4,
    parameter integer BURST_LEN      = 16
) (
    input wire clk, input wire rst_n,
    input wire cmd_valid, output wire cmd_ready,
    input wire [AXI_ADDR_WIDTH-1:0] cmd_addr,
    input wire [31:0] cmd_length_bytes,
    input wire [31:0] cmd_stride_bytes,
    input wire [6:0] cmd_rows,
    output wire bus_request, output wire bus_yield, input wire bus_grant,
    output wire burst_done, output wire busy, output wire done, output wire error,
    input wire fifo_valid, output wire fifo_ready, input wire [15:0] fifo_data,
    output wire [AXI_ID_WIDTH-1:0] m_axi_awid,
    output wire [AXI_ADDR_WIDTH-1:0] m_axi_awaddr,
    output wire [7:0] m_axi_awlen, output wire [2:0] m_axi_awsize,
    output wire [1:0] m_axi_awburst, output wire [3:0] m_axi_awcache,
    output wire [3:0] m_axi_awqos, output wire [3:0] m_axi_awregion,
    output wire [2:0] m_axi_awprot, output wire m_axi_awlock,
    output wire m_axi_awvalid, input wire m_axi_awready,
    output wire [31:0] m_axi_wdata, output wire [3:0] m_axi_wstrb,
    output wire m_axi_wlast, output wire m_axi_wvalid, input wire m_axi_wready,
    input wire [AXI_ID_WIDTH-1:0] m_axi_bid,
    input wire [1:0] m_axi_bresp, input wire m_axi_bvalid,
    output wire m_axi_bready
);
    localparam [1:0] PH_AW=2'd0, PH_W=2'd1, PH_B=2'd2;

    reg cmd_active, fill_active, send_active;
    reg fill_bank, send_bank, fill_bank_preference, send_expected_bank;
    reg [1:0] send_phase;
    reg [AXI_ADDR_WIDTH-1:0] issue_addr, row_start_addr;
    reg [31:0] issue_row_bytes, issue_row_bytes_left;
    reg [31:0] issue_stride;
    reg [6:0] issue_rows_left;
    reg [31:0] fill_bytes_left;
    reg [8:0] fill_word_index, send_word_index;
    reg fill_cmd_last;
    reg [1:0] fill_lane;
    reg [31:0] build_data;
    reg [3:0] build_strb;

    reg bank0_ready, bank1_ready;
    reg [AXI_ADDR_WIDTH-1:0] bank0_addr, bank1_addr;
    reg [8:0] bank0_beats, bank1_beats;
    reg bank0_last, bank1_last;
    reg [31:0] mem0 [0:BURST_LEN-1];
    reg [31:0] mem1 [0:BURST_LEN-1];
    reg [3:0] strb0 [0:BURST_LEN-1];
    reg [3:0] strb1 [0:BURST_LEN-1];

    reg pixel_valid, pixel_byte_sel;
    reg [15:0] pixel_data;
    reg done_reg, error_reg, burst_done_reg;

    // One-cycle descriptor stage separates burst calculation from the issue
    // cursor update, keeping the cross-row address path short.
    reg desc_valid, desc_bank, desc_row_last, desc_cmd_last;
    reg [AXI_ADDR_WIDTH-1:0] desc_addr;
    reg [8:0] desc_beats;
    reg [31:0] desc_bytes;
    reg [AXI_ADDR_WIDTH-1:0] desc_next_row_addr;
    reg calc_pending;
    reg [AXI_ADDR_WIDTH-1:0] calc_addr_q, calc_next_row_addr_q;
    reg [8:0] calc_beats_q;
    reg [31:0] calc_bytes_left_q;

    wire [8:0] calc_beats;
    wire [31:0] calc_bytes;
    wire [31:0] calc_stage_bytes = calc_burst_bytes(calc_addr_q,
                                                    calc_bytes_left_q,
                                                    calc_beats_q);
    wire bank0_free = !bank0_ready && !(send_active && !send_bank);
    wire bank1_free = !bank1_ready && !(send_active && send_bank);
    wire calc_request = cmd_active && !fill_active && !desc_valid &&
                        !calc_pending && (issue_rows_left != 0) &&
                        (bank0_free || bank1_free);
    wire desc_make = calc_pending;
    // Banks form a two-entry FIFO.  Do not select whichever bank happens to
    // become ready first: that can let a later final burst overtake an older
    // ready burst and assert done before all earlier pixels are written.
    wire selected_bank = send_expected_bank;
    wire selected_ready = selected_bank ? bank1_ready : bank0_ready;
    // Preserve a refill gap between bursts and prevent a same-cycle fill/send
    // start.  The old single-bank writer never filled during AXI transmission.
    wire fill_start = desc_valid && !send_active &&
                      !(selected_ready && bus_grant);

    wire [2:0] fill_capacity = 3'd4 - {1'b0,fill_lane};
    wire [2:0] fill_byte_count = (fill_bytes_left < fill_capacity) ?
                                  fill_bytes_left[2:0] : fill_capacity;
    wire halfword_consume = pixel_valid && !pixel_byte_sel &&
                            !fill_lane[0] && (fill_bytes_left >= 2);
    wire byte_consume = pixel_valid && !halfword_consume;
    wire pixel_consume = halfword_consume || pixel_byte_sel ||
                         (byte_consume && !pixel_byte_sel &&
                          (fill_bytes_left == 1) && fill_cmd_last);
    wire pixel_needs_refill = halfword_consume ?
                              ((fill_bytes_left > 2) || !fill_cmd_last) :
                              ((fill_bytes_left > 1) || !fill_cmd_last);
    assign fifo_ready = fill_active &&
                        ((!pixel_valid) || (pixel_consume && pixel_needs_refill));
    wire pixel_load = fifo_valid && fifo_ready;
    wire [7:0] pixel_byte = pixel_byte_sel ? pixel_data[15:8] : pixel_data[7:0];
    wire [31:0] build_data_with_byte =
        (fill_lane == 2'd0) ? {build_data[31:8],pixel_byte} :
        (fill_lane == 2'd1) ? {build_data[31:16],pixel_byte,build_data[7:0]} :
        (fill_lane == 2'd2) ? {build_data[31:24],pixel_byte,build_data[15:0]} :
                              {pixel_byte,build_data[23:0]};
    wire [3:0] build_strb_with_byte = build_strb | (4'b0001 << fill_lane);

    function [8:0] calc_burst_beats;
        input [AXI_ADDR_WIDTH-1:0] addr;
        input [31:0] bytes_left;
        reg [31:0] needed, result;
        reg [12:0] boundary_bytes;
        reg [10:0] boundary_beats;
        begin
            needed = ({30'd0,addr[1:0]} + bytes_left + 3) >> 2;
            boundary_bytes = 13'd4096 - {1'b0,addr[11:0]};
            boundary_beats = boundary_bytes >> 2;
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
            capacity = (beats << 2) - {30'd0,addr[1:0]};
            calc_burst_bytes = (bytes_left < capacity) ? bytes_left : capacity;
        end
    endfunction

    assign calc_beats = calc_burst_beats(issue_addr,issue_row_bytes_left);
    assign calc_bytes = calc_burst_bytes(issue_addr,issue_row_bytes_left,calc_beats);
    wire aw_fire = m_axi_awvalid && m_axi_awready;
    wire w_fire = m_axi_wvalid && m_axi_wready;
    wire b_fire = m_axi_bvalid && m_axi_bready;
    assign cmd_ready = !cmd_active;
    assign busy = cmd_active;
    assign done = done_reg;
    assign error = error_reg;
    assign burst_done = burst_done_reg;
    // Keep ownership across consecutive ready bursts. Yield only while the
    // next bank is still being filled, so another core can use the bus.
    assign bus_request = cmd_active && (send_active || selected_ready);
    // End the write ownership quantum at every completed burst.  The old
    // writer returned to the DMA arbiter after B; keeping ownership across a
    // whole batched command can starve the HDMI read side under heavy fill
    // load and causes scanout jitter.  The prepared bank remains ready and
    // will be granted again on the next arbitration cycle.
    // Release is driven by the registered burst_done pulse in the DMA
    // arbiter.  Do not release on the combinational B handshake itself: that
    // lets the owner mux change in the same edge as the AXI response.
    assign bus_yield = cmd_active && !send_active && !selected_ready;

    wire [AXI_ADDR_WIDTH-1:0] send_addr = send_bank ? bank1_addr : bank0_addr;
    wire [8:0] send_beats = send_bank ? bank1_beats : bank0_beats;
    wire send_last = send_bank ? bank1_last : bank0_last;
    assign m_axi_awid = {AXI_ID_WIDTH{1'b0}};
    assign m_axi_awaddr = send_addr;
    assign m_axi_awlen = send_beats - 1'b1;
    assign m_axi_awsize = 3'd2;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awqos = 4'd0;
    assign m_axi_awregion = 4'd0;
    assign m_axi_awprot = 3'd0;
    assign m_axi_awlock = 1'b0;
    assign m_axi_awvalid = send_active && (send_phase == PH_AW);
    assign m_axi_wdata = send_bank ? mem1[send_word_index] : mem0[send_word_index];
    assign m_axi_wstrb = send_bank ? strb1[send_word_index] : strb0[send_word_index];
    assign m_axi_wlast = (send_word_index == (send_beats - 1'b1));
    assign m_axi_wvalid = send_active && (send_phase == PH_W);
    assign m_axi_bready = send_active && (send_phase == PH_B);
    always @(posedge clk) begin
        if (!rst_n) begin
            cmd_active <= 1'b0; fill_active <= 1'b0; send_active <= 1'b0;
            fill_bank <= 1'b0; send_bank <= 1'b0; fill_bank_preference <= 1'b0;
            send_expected_bank <= 1'b0;
            send_phase <= PH_AW;
            issue_addr <= 0; row_start_addr <= 0; issue_row_bytes <= 0;
            issue_row_bytes_left <= 0; issue_stride <= 0;
            issue_rows_left <= 0;
            fill_bytes_left <= 0; fill_word_index <= 0;
            send_word_index <= 0; fill_cmd_last <= 1'b0; fill_lane <= 0;
            build_data <= 0; build_strb <= 0;
            bank0_ready <= 1'b0; bank1_ready <= 1'b0;
            bank0_addr <= 0; bank1_addr <= 0; bank0_beats <= 0; bank1_beats <= 0;
            bank0_last <= 1'b0; bank1_last <= 1'b0;
            desc_valid <= 1'b0; desc_bank <= 1'b0;
            desc_row_last <= 1'b0; desc_cmd_last <= 1'b0;
            desc_addr <= 0; desc_beats <= 0; desc_bytes <= 0;
            desc_next_row_addr <= 0;
            calc_pending <= 1'b0; calc_addr_q <= 0; calc_next_row_addr_q <= 0;
            calc_beats_q <= 0; calc_bytes_left_q <= 0;
            pixel_valid <= 1'b0; pixel_byte_sel <= 1'b0; pixel_data <= 0;
            done_reg <= 1'b0; error_reg <= 1'b0; burst_done_reg <= 1'b0;
        end else begin
            done_reg <= 1'b0;
            burst_done_reg <= 1'b0;

            if (calc_request) begin
                calc_pending <= 1'b1;
                calc_addr_q <= issue_addr;
                calc_beats_q <= calc_beats;
                calc_bytes_left_q <= issue_row_bytes_left;
                calc_next_row_addr_q <= row_start_addr + issue_stride;
            end

            if (desc_make) begin
                calc_pending <= 1'b0;
                desc_valid <= 1'b1;
                desc_bank <= (bank0_free && bank1_free) ? fill_bank_preference :
                             bank0_free ? 1'b0 : 1'b1;
                desc_addr <= calc_addr_q;
                desc_beats <= calc_beats_q;
                desc_bytes <= calc_stage_bytes;
                desc_row_last <= (calc_bytes_left_q == calc_stage_bytes);
                desc_cmd_last <= (issue_rows_left == 1) &&
                                 (calc_bytes_left_q == calc_stage_bytes);
                desc_next_row_addr <= calc_next_row_addr_q;
            end

            if (!cmd_active) begin
                if (cmd_valid) begin
                    error_reg <= 1'b0;
                    if (cmd_length_bytes == 0 || cmd_rows == 0) begin
                        done_reg <= 1'b1;
                    end else begin
                        cmd_active <= 1'b1;
                        issue_addr <= cmd_addr;
                        row_start_addr <= cmd_addr;
                        issue_row_bytes <= cmd_length_bytes;
                        issue_row_bytes_left <= cmd_length_bytes;
                        issue_stride <= cmd_stride_bytes;
                        issue_rows_left <= cmd_rows;
                        fill_bank_preference <= 1'b0;
                        send_expected_bank <= 1'b0;
                        bank0_ready <= 1'b0; bank1_ready <= 1'b0;
                        fill_active <= 1'b0; send_active <= 1'b0;
                        desc_valid <= 1'b0;
                        calc_pending <= 1'b0;
                        pixel_valid <= 1'b0; pixel_byte_sel <= 1'b0;
                    end
                end
            end else begin
                if (fill_start) begin
                    desc_valid <= 1'b0;
                    fill_active <= 1'b1;
                    fill_bank <= desc_bank;
                    fill_bank_preference <= ~desc_bank;
                    fill_bytes_left <= desc_bytes;
                    fill_cmd_last <= desc_cmd_last;
                    fill_lane <= desc_addr[1:0];
                    fill_word_index <= 0;
                    build_data <= 0; build_strb <= 0;
                    if (desc_row_last) begin
                        issue_rows_left <= issue_rows_left - 1'b1;
                        issue_addr <= desc_next_row_addr;
                        row_start_addr <= desc_next_row_addr;
                        issue_row_bytes_left <= (issue_rows_left == 1) ? 0 :
                                                issue_row_bytes;
                    end else begin
                        issue_addr <= issue_addr + desc_bytes;
                        issue_row_bytes_left <= issue_row_bytes_left - desc_bytes;
                    end
                    if (!desc_bank) begin
                        bank0_addr <= {desc_addr[AXI_ADDR_WIDTH-1:2],2'b00};
                        bank0_beats <= desc_beats;
                        bank0_last <= desc_cmd_last;
                    end else begin
                        bank1_addr <= {desc_addr[AXI_ADDR_WIDTH-1:2],2'b00};
                        bank1_beats <= desc_beats;
                        bank1_last <= desc_cmd_last;
                    end
                end

                if (fill_active) begin
                    if (!pixel_valid) begin
                        if (pixel_load) begin
                            pixel_valid <= 1'b1;
                            pixel_data <= fifo_data;
                            pixel_byte_sel <= 1'b0;
                        end
                    end else if (halfword_consume) begin
                        if ((fill_lane == 2'd2) || (fill_bytes_left == 2)) begin
                            if (!fill_bank) begin
                                mem0[fill_word_index] <= fill_lane[1] ? {pixel_data,build_data[15:0]} : {build_data[31:16],pixel_data};
                                strb0[fill_word_index] <= build_strb | (fill_lane[1] ? 4'b1100 : 4'b0011);
                            end else begin
                                mem1[fill_word_index] <= fill_lane[1] ? {pixel_data,build_data[15:0]} : {build_data[31:16],pixel_data};
                                strb1[fill_word_index] <= build_strb | (fill_lane[1] ? 4'b1100 : 4'b0011);
                            end
                            fill_word_index <= fill_word_index + 1'b1;
                            build_data <= 0; build_strb <= 0;
                        end else begin
                            build_data <= {build_data[31:16],pixel_data};
                            build_strb <= build_strb | 4'b0011;
                        end
                        fill_lane <= fill_lane + 2'd2;
                        fill_bytes_left <= fill_bytes_left - 2;
                        pixel_valid <= pixel_load;
                        pixel_byte_sel <= 1'b0;
                        if (pixel_load) pixel_data <= fifo_data;
                        if (fill_bytes_left == 2) begin
                            fill_active <= 1'b0;
                            if (!fill_bank) bank0_ready <= 1'b1;
                            else bank1_ready <= 1'b1;
                        end
                    end else begin
                        if ((fill_lane == 3) || (fill_bytes_left == 1)) begin
                            if (!fill_bank) begin
                                mem0[fill_word_index] <= build_data_with_byte;
                                strb0[fill_word_index] <= build_strb_with_byte;
                            end else begin
                                mem1[fill_word_index] <= build_data_with_byte;
                                strb1[fill_word_index] <= build_strb_with_byte;
                            end
                            fill_word_index <= fill_word_index + 1'b1;
                            build_data <= 0; build_strb <= 0;
                        end else begin
                            build_data <= build_data_with_byte;
                            build_strb <= build_strb_with_byte;
                        end
                        fill_lane <= fill_lane + 1'b1;
                        fill_bytes_left <= fill_bytes_left - 1'b1;
                        if (!pixel_byte_sel) begin
                            if ((fill_bytes_left == 1) && fill_cmd_last) begin
                                pixel_valid <= 1'b0; pixel_byte_sel <= 1'b0;
                            end else begin
                                pixel_valid <= 1'b1; pixel_byte_sel <= 1'b1;
                            end
                        end else begin
                            pixel_valid <= pixel_load;
                            pixel_byte_sel <= 1'b0;
                            if (pixel_load) pixel_data <= fifo_data;
                        end
                        if (fill_bytes_left == 1) begin
                            fill_active <= 1'b0;
                            if (!fill_bank) bank0_ready <= 1'b1;
                            else bank1_ready <= 1'b1;
                        end
                    end
                end

                if (!send_active && selected_ready && bus_grant) begin
                    send_active <= 1'b1;
                    send_bank <= selected_bank;
                    send_expected_bank <= ~selected_bank;
                    send_phase <= PH_AW;
                    if (!selected_bank) bank0_ready <= 1'b0;
                    else bank1_ready <= 1'b0;
                end else if (send_active) begin
                    if ((send_phase == PH_AW) && aw_fire) begin
                        send_phase <= PH_W;
                        send_word_index <= 0;
                    end else if ((send_phase == PH_W) && w_fire) begin
                        if (m_axi_wlast) send_phase <= PH_B;
                        else send_word_index <= send_word_index + 1'b1;
                    end else if ((send_phase == PH_B) && b_fire) begin
                        send_active <= 1'b0;
                        burst_done_reg <= 1'b1;
                        if (m_axi_bresp != 2'b00) error_reg <= 1'b1;
                        if (send_last) begin
                            cmd_active <= 1'b0;
                            done_reg <= 1'b1;
                        end
                    end
                end
            end
        end
    end

    wire unused_bid = ^m_axi_bid;
endmodule
