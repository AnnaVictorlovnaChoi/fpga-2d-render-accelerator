`timescale 1ns/1ps
module gpu_sync_fifo #(
    parameter integer WIDTH = 32,
    parameter integer DEPTH = 256,
    parameter integer PROG_FULL_LEVEL = DEPTH-4,
    parameter integer WR_WIDTH = WIDTH,
    parameter integer RD_WIDTH = WIDTH
) (
    input wire clk, input wire rst_n,
    input wire wr_en, input wire [WR_WIDTH-1:0] wr_data,
    input wire [WR_WIDTH/RD_WIDTH-1:0] wr_keep,
    input wire rd_en, output wire [RD_WIDTH-1:0] rd_data,
    output wire full, output wire prog_full, output wire empty,
    output wire [7:0] count_used
);
    localparam integer RATIO = WR_WIDTH / RD_WIDTH;
    localparam integer KEEP_WIDTH = (RATIO > 1) ? RATIO : 1;
    localparam integer MEM_DEPTH = (DEPTH + RATIO - 1) / RATIO;
    localparam integer PTR_WIDTH = (MEM_DEPTH <= 2) ? 1 : $clog2(MEM_DEPTH);
    localparam integer COUNT_WIDTH = (DEPTH < 2) ? 1 : $clog2(DEPTH + 1);
    reg [WR_WIDTH-1:0] mem [0:MEM_DEPTH-1];
    reg [KEEP_WIDTH-1:0] mem_keep [0:MEM_DEPTH-1];
    reg [PTR_WIDTH-1:0] wr_ptr, rd_ptr;
    reg [COUNT_WIDTH-1:0] count;
    localparam integer LANE_WIDTH = (RATIO <= 1) ? 1 : $clog2(RATIO);
    localparam integer ENTRY_WIDTH = $clog2(MEM_DEPTH+1);
    reg [ENTRY_WIDTH-1:0] entries;
    reg [LANE_WIDTH-1:0] rd_lane;
    reg [LANE_WIDTH-1:0] selected_lane;
    reg [LANE_WIDTH:0] write_words;
    reg lane_found, more_lanes;
    integer k;
    // Small (one, two or four lane) selection. Supports partial/sparse masks.
    always @* begin
        write_words = 0;
        selected_lane = 0;
        lane_found = 0;
        more_lanes = 0;
        for (k=0; k<RATIO; k=k+1) begin
            if (RATIO == 1 || wr_keep[k]) write_words = write_words + 1'b1;
            if (k >= rd_lane && (RATIO == 1 || mem_keep[rd_ptr][k])) begin
                if (!lane_found) begin
                    selected_lane = k;
                    lane_found = 1;
                end else more_lanes = 1;
            end
        end
    end
    wire do_write = wr_en && !full && (write_words != 0);
    wire do_read = rd_en && !empty;
    wire advance_word = !more_lanes;
    wire retire_word = do_read && advance_word;
    assign rd_data = mem[rd_ptr][selected_lane*RD_WIDTH +: RD_WIDTH];
    assign empty = (entries == 0);
    // A partial word still consumes a RAM entry. Pixel count alone is unsafe.
    assign full = (entries == MEM_DEPTH) || (count > DEPTH-RATIO);
    assign prog_full = full || (count >= PROG_FULL_LEVEL);
    // Implicit extension/truncation keeps this fixed 8-bit interface valid
    // for both small command FIFOs and deeper pixel FIFOs.
    assign count_used = count;
    always @(posedge clk) begin
        if (!rst_n) begin
            wr_ptr <= 0; rd_ptr <= 0; rd_lane <= 0; count <= 0; entries <= 0;
        end else begin
            if (do_write) begin
                mem[wr_ptr] <= wr_data;
                mem_keep[wr_ptr] <= (RATIO > 1) ? wr_keep : 1'b1;
                if (wr_ptr == MEM_DEPTH-1) wr_ptr <= 0; else wr_ptr <= wr_ptr + 1'b1;
            end
            if (do_read) begin
                if (advance_word) begin
                    rd_lane <= 0;
                    if (rd_ptr == MEM_DEPTH-1) rd_ptr <= 0; else rd_ptr <= rd_ptr + 1'b1;
                end else rd_lane <= selected_lane + 1'b1;
            end
            case ({do_write,retire_word})
                2'b10: entries <= entries + 1'b1;
                2'b01: entries <= entries - 1'b1;
                default: entries <= entries;
            endcase
            case ({do_write,do_read})
                2'b10: count <= count + write_words;
                2'b01: count <= count - 1'b1;
                2'b11: count <= count + write_words - 1'b1;
                default: count <= count;
            endcase
        end
    end
endmodule
