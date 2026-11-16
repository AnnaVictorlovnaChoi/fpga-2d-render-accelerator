`timescale 1ns/1ps

// AXI4 write engine for a byte-addressed RGB565 stream.
//
// The input is a 16-bit ready/valid stream in increasing byte-address order.
// Each burst is staged in the local buffer before requesting the shared AXI
// owner. Arbitrary byte start addresses and lengths use aligned AWADDR/WSTRB.
module axi_rgb565_writer #(
    parameter integer AXI_ADDR_WIDTH = 32,
    parameter integer AXI_ID_WIDTH   = 4,
    parameter integer BURST_LEN      = 8
) (
    input  wire                        clk,
    input  wire                        rst_n,

    input  wire                        cmd_valid,
    output wire                        cmd_ready,
    input  wire [AXI_ADDR_WIDTH-1:0]   cmd_addr,
    input  wire [31:0]                 cmd_length_bytes,
    input  wire                        cmd_last,
    input  wire                        cmd_constant_fill,
    input  wire [15:0]                 cmd_constant_data,
    output wire                        bus_request,
    output wire                        bus_release,
    input  wire                        bus_abort,
    input  wire                        bus_grant,
    output wire                        burst_done,
    output wire                        busy,
    output wire                        done,
    output wire                        error,

    input  wire                        fifo_valid,
    output wire                        fifo_ready,
    input  wire [15:0]                 fifo_data,

    output wire [AXI_ID_WIDTH-1:0]     m_axi_awid,
    output wire [AXI_ADDR_WIDTH-1:0]   m_axi_awaddr,
    output wire [7:0]                  m_axi_awlen,
    output wire [2:0]                  m_axi_awsize,
    output wire [1:0]                  m_axi_awburst,
    output wire [3:0]                  m_axi_awcache,
    output wire [3:0]                  m_axi_awqos,
    output wire [3:0]                  m_axi_awregion,
    output wire [2:0]                  m_axi_awprot,
    output wire                        m_axi_awlock,
    output wire                        m_axi_awvalid,
    input  wire                        m_axi_awready,

    output wire [63:0]                 m_axi_wdata,
    output wire [7:0]                  m_axi_wstrb,
    output wire                        m_axi_wlast,
    output wire                        m_axi_wvalid,
    input  wire                        m_axi_wready,

    input  wire [AXI_ID_WIDTH-1:0]     m_axi_bid,
    input  wire [1:0]                  m_axi_bresp,
    input  wire                        m_axi_bvalid,
    output wire                        m_axi_bready
);

    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_PREP = 3'd1;
    localparam [2:0] ST_FILL = 3'd2;
    localparam [2:0] ST_AW   = 3'd3;
    localparam [2:0] ST_W    = 3'd4;
    localparam [2:0] ST_B    = 3'd5;
    localparam [2:0] ST_GRANT= 3'd6;
    localparam [2:0] ST_CONST_PREP = 3'd7;

    reg [2:0] state;
    reg [AXI_ADDR_WIDTH-1:0] current_addr;
    reg [31:0] remaining_bytes;
    reg [AXI_ADDR_WIDTH-1:0] burst_addr;
    reg [8:0] burst_beats;
    reg [31:0] burst_bytes;
    reg [31:0] fill_bytes_left;
    reg [2:0] fill_lane;
    reg [8:0] fill_word_index;
    reg [8:0] send_word_index;
    reg [63:0] build_data;
    reg [7:0] build_strb;
    reg [7:0] constant_first_strb;
    reg [7:0] constant_last_strb;
    reg constant_fill_q;
    reg [15:0] constant_data_q;

    // One cached RGB565 pixel. A consumed pixel can be replaced from the
    // input FIFO on the same edge, keeping the aligned stream at one pixel
    // per cycle while still allowing pixels to span burst boundaries.
    reg        pixel_valid;
    reg [15:0] pixel_data;
    reg        pixel_byte_sel; // 0 = low byte next, 1 = high byte next

    reg [63:0] burst_data_mem [0:BURST_LEN-1];
    reg [7:0] burst_strb_mem [0:BURST_LEN-1];

    reg done_reg;
    reg error_reg;
    reg burst_done_reg;
    reg bus_release_reg;

    wire [8:0] calculated_burst_beats;
    wire [31:0] calculated_burst_bytes;
    wire pixel_load;
    wire [7:0] pixel_byte;
    wire [63:0] build_data_with_byte;
    wire [7:0] build_strb_with_byte;
    wire [3:0] constant_end_lane;
    wire halfword_consume;
    wire byte_consume;
    wire pixel_consume;
    wire pixel_needs_refill;
    wire aw_fire;
    wire w_fire;
    wire b_fire;

    function [8:0] calc_burst_beats;
        input [AXI_ADDR_WIDTH-1:0] addr;
        input [31:0] bytes_left_arg;
        reg [31:0] beats_needed;
        reg [12:0] boundary_bytes;
        reg [10:0] boundary_beats;
        reg [31:0] result;
        begin
            beats_needed = ({29'd0, addr[2:0]} + bytes_left_arg + 7) >> 3;
            boundary_bytes = 13'd4096 - {1'b0, addr[11:0]};
            boundary_beats = boundary_bytes >> 3;
            result = beats_needed;
            if (result > BURST_LEN)
                result = BURST_LEN;
            if (result > boundary_beats)
                result = boundary_beats;
            if (result == 0)
                result = 1;
            calc_burst_beats = result[8:0];
        end
    endfunction

    function [31:0] calc_burst_bytes;
        input [AXI_ADDR_WIDTH-1:0] addr;
        input [31:0] bytes_left_arg;
        input [8:0] beats_arg;
        reg [31:0] capacity;
        begin
            capacity = (beats_arg << 3) - {29'd0, addr[2:0]};
            if (bytes_left_arg < capacity)
                calc_burst_bytes = bytes_left_arg;
            else
                calc_burst_bytes = capacity;
        end
    endfunction

    function [7:0] low_byte_mask;
        input [2:0] byte_count_mod8;
        begin
            case (byte_count_mod8)
                3'd1: low_byte_mask = 8'h01;
                3'd2: low_byte_mask = 8'h03;
                3'd3: low_byte_mask = 8'h07;
                3'd4: low_byte_mask = 8'h0F;
                3'd5: low_byte_mask = 8'h1F;
                3'd6: low_byte_mask = 8'h3F;
                3'd7: low_byte_mask = 8'h7F;
                default: low_byte_mask = 8'hFF;
            endcase
        end
    endfunction

    // The constant-fill path only handles even RGB565 byte ranges. Prepare
    // edge strobes once per burst; the beat loop then uses only its index.
    assign constant_end_lane = {1'b0,current_addr[2:0]} +
                               {1'b0,burst_bytes[2:0]};

    assign cmd_ready = (state == ST_IDLE);
    assign busy = (state != ST_IDLE);
    assign done = done_reg;
    assign error = error_reg;
    assign bus_request = (state == ST_GRANT);
    assign bus_release = bus_release_reg;
    assign burst_done = burst_done_reg;

    assign calculated_burst_beats = calc_burst_beats(current_addr,
                                                      remaining_bytes);
    assign calculated_burst_bytes = calc_burst_bytes(current_addr,
                                                      remaining_bytes,
                                                      calculated_burst_beats);

    assign halfword_consume = pixel_valid && !pixel_byte_sel &&
                              !fill_lane[0] && (fill_bytes_left >= 2);
    assign byte_consume = pixel_valid && !halfword_consume;
    assign pixel_consume = halfword_consume || pixel_byte_sel ||
                           (byte_consume && !pixel_byte_sel &&
                            (fill_bytes_left == 1) &&
                            (remaining_bytes == burst_bytes));
    assign pixel_needs_refill = halfword_consume ?
                                ((fill_bytes_left > 2) ||
                                 (remaining_bytes > burst_bytes)) :
                                ((fill_bytes_left > 1) ||
                                 (remaining_bytes > burst_bytes));
    // Refill as the current pixel is consumed. Do not pull an unused pixel
    // after the final command byte.
    assign fifo_ready = (state == ST_FILL) && !constant_fill_q &&
                        ((!pixel_valid) ||
                         (pixel_consume && pixel_needs_refill));
    assign pixel_load = fifo_valid && fifo_ready;
    assign pixel_byte = pixel_byte_sel ? pixel_data[15:8] : pixel_data[7:0];

    assign build_data_with_byte =
        build_data | ({56'd0,pixel_byte} << (fill_lane * 8));
    assign build_strb_with_byte = build_strb | (8'b00000001 << fill_lane);

    assign m_axi_awid     = {AXI_ID_WIDTH{1'b0}};
    assign m_axi_awaddr   = burst_addr;
    assign m_axi_awlen    = burst_beats - 1'b1;
    assign m_axi_awsize   = 3'd3;
    assign m_axi_awburst  = 2'b01;
    assign m_axi_awcache  = 4'b0011;
    assign m_axi_awqos    = 4'b0000;
    assign m_axi_awregion = 4'b0000;
    assign m_axi_awprot   = 3'b000;
    assign m_axi_awlock   = 1'b0;
    assign m_axi_awvalid  = (state == ST_AW);
    assign aw_fire = m_axi_awvalid && m_axi_awready;

    assign m_axi_wdata  = burst_data_mem[send_word_index];
    assign m_axi_wstrb  = burst_strb_mem[send_word_index];
    assign m_axi_wlast  = (send_word_index == (burst_beats - 1'b1));
    assign m_axi_wvalid = (state == ST_W);
    assign w_fire = m_axi_wvalid && m_axi_wready;

    assign m_axi_bready = (state == ST_B);
    assign b_fire = m_axi_bvalid && m_axi_bready;

    always @(posedge clk) begin
        if (!rst_n) begin
            state             <= ST_IDLE;
            current_addr      <= {AXI_ADDR_WIDTH{1'b0}};
            remaining_bytes   <= 32'd0;
            burst_addr        <= {AXI_ADDR_WIDTH{1'b0}};
            burst_beats       <= 9'd1;
            burst_bytes       <= 32'd0;
            fill_bytes_left   <= 32'd0;
            fill_lane         <= 3'd0;
            fill_word_index   <= 9'd0;
            send_word_index   <= 9'd0;
            build_data        <= 64'd0;
            build_strb        <= 8'd0;
            constant_first_strb <= 8'hFF;
            constant_last_strb  <= 8'hFF;
            constant_fill_q  <= 1'b0;
            constant_data_q  <= 16'd0;
            pixel_valid      <= 1'b0;
            pixel_data       <= 16'd0;
            pixel_byte_sel   <= 1'b0;
            done_reg          <= 1'b0;
            error_reg         <= 1'b0;
            burst_done_reg    <= 1'b0;
            bus_release_reg   <= 1'b0;
        end
        else begin
            done_reg <= 1'b0;
            burst_done_reg <= 1'b0;
            bus_release_reg <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (cmd_valid) begin
                        error_reg       <= 1'b0;
                        pixel_valid    <= 1'b0;
                        pixel_byte_sel <= 1'b0;
                        constant_fill_q <= cmd_constant_fill &&
                                           !cmd_addr[0] &&
                                           !cmd_length_bytes[0];
                        constant_data_q <= cmd_constant_data;
                        if (cmd_length_bytes == 0) begin
                            done_reg <= 1'b1;
                        end
                        else begin
                            current_addr    <= cmd_addr;
                            remaining_bytes <= cmd_length_bytes;
                            state <= ST_PREP;
                        end
                    end
                end

                ST_PREP: begin
                    burst_addr      <= {current_addr[AXI_ADDR_WIDTH-1:3], 3'b000};
                    burst_beats     <= calculated_burst_beats;
                    burst_bytes     <= calculated_burst_bytes;
                    fill_bytes_left <= calculated_burst_bytes;
                    fill_lane       <= current_addr[2:0];
                    fill_word_index <= 9'd0;
                    build_data      <= 64'd0;
                    build_strb      <= 8'd0;
                    if (constant_fill_q)
                        state <= ST_CONST_PREP;
                    else
                        state <= ST_FILL;
                end

                ST_CONST_PREP: begin
                    constant_first_strb <= 8'hFF << current_addr[2:0];
                    constant_last_strb <= low_byte_mask(constant_end_lane[2:0]);
                    state <= ST_FILL;
                end

                ST_FILL: begin
                    if (constant_fill_q) begin
                        // Prepare one complete 64-bit AXI beat per cycle.
                        burst_data_mem[fill_word_index] <= {4{constant_data_q}};
                        if (burst_beats == 1)
                            burst_strb_mem[fill_word_index] <=
                                constant_first_strb & constant_last_strb;
                        else if (fill_word_index == 0)
                            burst_strb_mem[fill_word_index] <= constant_first_strb;
                        else if (fill_word_index == (burst_beats - 1'b1))
                            burst_strb_mem[fill_word_index] <= constant_last_strb;
                        else
                            burst_strb_mem[fill_word_index] <= 8'hFF;
                        fill_word_index <= fill_word_index + 1'b1;
                        fill_lane <= 3'd0;
                        if (fill_word_index == (burst_beats - 1'b1))
                            state <= ST_GRANT;
                    end
                    else if (!pixel_valid) begin
                        if (pixel_load) begin
                            pixel_valid    <= 1'b1;
                            pixel_data     <= fifo_data;
                            pixel_byte_sel <= 1'b0;
                        end
                    end
                    else if (halfword_consume) begin
                        // Consume one aligned RGB565 pixel. Refill the cache
                        // on this same edge when another pixel is required.
                        if (fill_lane >= 3'd6 || fill_bytes_left == 2) begin
                            burst_data_mem[fill_word_index] <= build_data |
                                ({48'd0,pixel_data} << (fill_lane * 8));
                            burst_strb_mem[fill_word_index] <= build_strb |
                                (8'b00000011 << fill_lane);
                            fill_word_index <= fill_word_index + 1'b1;
                            build_data <= 64'd0;
                            build_strb <= 8'd0;
                        end else begin
                            build_data <= build_data | ({48'd0,pixel_data} << (fill_lane * 8));
                            build_strb <= build_strb | (8'b00000011 << fill_lane);
                        end
                        fill_lane <= fill_lane + 3'd2;
                        fill_bytes_left <= fill_bytes_left - 32'd2;
                        pixel_valid <= pixel_load;
                        pixel_byte_sel <= 1'b0;
                        if (pixel_load)
                            pixel_data <= fifo_data;
                        if (fill_bytes_left == 2)
                            state <= ST_GRANT;
                    end
                    else begin
                        // Preserve byte-addressed commands and split pixels.
                        if ((fill_lane == 7) || (fill_bytes_left == 1)) begin
                            burst_data_mem[fill_word_index] <=
                                build_data_with_byte;
                            burst_strb_mem[fill_word_index] <=
                                build_strb_with_byte;
                            fill_word_index <= fill_word_index + 1'b1;
                            build_data <= 64'd0;
                            build_strb <= 8'd0;
                        end
                        else begin
                            build_data <= build_data_with_byte;
                            build_strb <= build_strb_with_byte;
                        end

                        fill_lane       <= fill_lane + 1'b1;
                        fill_bytes_left <= fill_bytes_left - 1'b1;

                        // Low byte leaves the cached pixel valid for the
                        // high byte.  If this is the final command byte,
                        // explicitly discard the unused high byte.
                        if (!pixel_byte_sel) begin
                            if ((fill_bytes_left == 1) &&
                                (remaining_bytes == burst_bytes)) begin
                                pixel_valid    <= 1'b0;
                                pixel_byte_sel <= 1'b0;
                            end
                            else begin
                                pixel_valid    <= 1'b1;
                                pixel_byte_sel <= 1'b1;
                            end
                        end
                        else begin
                            pixel_valid    <= pixel_load;
                            pixel_byte_sel <= 1'b0;
                            if (pixel_load)
                                pixel_data <= fifo_data;
                        end

                        if (fill_bytes_left == 1)
                            state <= ST_GRANT;
                    end
                end

                ST_GRANT: begin
                    if (bus_grant)
                        state <= ST_AW;
                end

                ST_AW: begin
                    if (aw_fire) begin
                        send_word_index <= 9'd0;
                        state <= ST_W;
                    end
                end

                ST_W: begin
                    if (w_fire) begin
                        if (m_axi_wlast)
                            state <= ST_B;
                        else
                            send_word_index <= send_word_index + 1'b1;
                    end
                end

                ST_B: begin
                    if (b_fire) begin
                        burst_done_reg <= 1'b1;
                        if (m_axi_bresp != 2'b00)
                            error_reg <= 1'b1;
                        bus_release_reg <= 1'b1;
                        current_addr    <= current_addr + burst_bytes;
                        remaining_bytes <= remaining_bytes - burst_bytes;
                        if (remaining_bytes == burst_bytes) begin
                            done_reg <= 1'b1;
                            state <= ST_IDLE;
                        end
                        else begin
                            state <= ST_PREP;
                        end
                    end
                end


                default: state <= ST_IDLE;
            endcase
        end
    end

    // BID is intentionally not compared because this engine uses one fixed ID
    // and permits only one outstanding write burst.
    wire unused_bid = ^m_axi_bid;

endmodule
