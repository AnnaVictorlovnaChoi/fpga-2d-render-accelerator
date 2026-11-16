`timescale 1ns/1ps
// Four shared 1024-pixel source-image slots. Same-slot command ordering is
// enforced by gpu_schduler8. This controller serializes source batches only;
// destination reads and writes keep their existing DMA paths.
module gpu_source_cache (
    input wire clk, input wire rst_n,
    input wire start, output wire ready,
    input wire [2:0] operation, input wire [3:0] control,
    input wire [31:0] length_bytes,
    input wire [6:0] rows, input wire [15:0] first_row,
    input wire last_batch,
    output wire busy, output wire done, output wire error,
    output wire ddr_start,
    input wire ddr_done, input wire ddr_error,
    input wire in_valid, input wire [63:0] in_data,
    input wire [3:0] in_keep, input wire in_last,
    output wire in_full,
    input wire fifo_full,
    output wire out_valid, output wire [63:0] out_data,
    output wire [3:0] out_keep, output wire out_last
);
    localparam IDLE=0, SETUP=1, BOUNDS=2, CHECK=3, LAUNCH=4, DDR=5,
               FLUSH=6, COMMIT=7, RAM_READ=8, FINISH=11, OFFSETS=12;
    reg [3:0] state;
    reg [1:0] bank;
    reg use_slot, fill_slot, final_batch, failed, ddr_finished;
    reg [15:0] req_width, req_first_row;
    reg [16:0] req_end_row;
    reg [6:0] req_rows, rows_left;
    reg [15:0] widths[0:3];
    reg [10:0] counts[0:3];
    reg [3:0] valid_slots;
    reg [63:0] pack_word[0:3];
    reg [31:0] first_offset, last_offset, end_offset;
    reg [15:0] pitch;
    reg [10:0] row_base, pixel_offset;
    reg [10:0] row_left;
    reg [63:0] capture_data;
    reg [3:0] capture_keep;
    reg replay_valid, issued_all;
    reg [1:0] replay_lane;
    reg [3:0] replay_keep;
    reg replay_last;
    reg ram_we;
    reg [7:0] write_address;
    reg [63:0] write_data;
    wire [63:0] ram_data[0:3];
    wire [7:0] ram_addr = ram_we ? write_address : pixel_offset[9:2];
    wire ram_re = (state == RAM_READ) && !issued_all && (!replay_valid || !fifo_full);
    genvar b;
    generate for(b=0;b<4;b=b+1) begin: BANK
        GPU_CACHE ram(.clk(clk),.reset(!rst_n),.re(ram_re && bank==b),
            .we(ram_we && bank==b),.addren((ram_re || ram_we) && bank==b),
            .addr(ram_addr),.wdata_a(write_data),.rdata_a(ram_data[b]));
    end endgenerate
    wire allowed_op = operation==0 || operation==2 || operation==3 || operation==5;
    wire allowed_slot = control[2:0]>=1 && control[2:0]<=4;
    wire [2:0] available_pixels = 3'd4 - {1'b0,pixel_offset[1:0]};
    wire [2:0] next_pixels = (row_left < available_pixels) ? row_left[2:0] : available_pixels;
    wire [63:0] pixel_mask = 64'hffff << {counts[bank][1:0],4'b0000};
    wire [63:0] packed_next = (pack_word[bank] & ~pixel_mask) |
                            ({48'd0,capture_data[15:0]} << {counts[bank][1:0],4'b0000});
    assign ready = state==IDLE;
    assign busy = state!=IDLE;
    assign done = state==FINISH;
    assign error = done && failed;
    assign ddr_start = state==LAUNCH;
    // Holding RREADY while serializing a fill beat preserves every valid
    // halfword, including short/unaligned row ends, without extra RAM.
    assign in_full = fifo_full || (fill_slot && capture_keep!=0);
    assign out_valid = (state==DDR && in_valid) || (state==RAM_READ && replay_valid && !fifo_full);
    assign out_data = state==RAM_READ ? (ram_data[bank] >> {replay_lane,4'b0000}) : in_data;
    assign out_keep = state==RAM_READ ? replay_keep : in_keep;
    assign out_last = state==RAM_READ ? replay_last : in_last;
    integer i;
    always @(posedge clk) begin
        if(!rst_n) begin
            state<=IDLE; bank<=0; use_slot<=0; fill_slot<=0; final_batch<=0;
            failed<=0; ddr_finished<=0; req_width<=0; req_first_row<=0; req_end_row<=0; req_rows<=0;
            rows_left<=0; valid_slots<=0; first_offset<=0; last_offset<=0; end_offset<=0;
            pitch<=0; row_base<=0; pixel_offset<=0; row_left<=0;
            capture_data<=0; capture_keep<=0; replay_valid<=0; issued_all<=0; replay_lane<=0; replay_keep<=0;
            replay_last<=0; ram_we<=0; write_address<=0; write_data<=0;
            for(i=0;i<4;i=i+1) begin widths[i]<=0; counts[i]<=0; pack_word[i]<=0; end
        end else begin
            ram_we<=0;
            case(state)
                IDLE: if(start) begin
                    bank<=control[2:0]-1'b1;
                    use_slot<=allowed_op && allowed_slot;
                    fill_slot<=allowed_op && allowed_slot && control[3];
                    req_width<=length_bytes[16:1]; req_first_row<=first_row;
                    req_end_row<={1'b0,first_row}+{10'd0,rows}-17'd1;
                    req_rows<=rows; final_batch<=last_batch; failed<=0; ddr_finished<=0;
                    capture_keep<=0; replay_valid<=0; issued_all<=0;
                    state<=SETUP;
                end
                SETUP: begin
                    // Registered products avoid a multiply/compare/mux path.
                    pitch<=widths[bank];

                    if(fill_slot && req_first_row==0) begin
                        valid_slots[bank]<=0; counts[bank]<=0;
                        widths[bank]<=req_width; pack_word[bank]<=0;
                    end
                    state<=OFFSETS;
                end
                OFFSETS: begin
                    first_offset<=req_first_row * pitch;
                    last_offset<=req_end_row * pitch;
                    state<=BOUNDS;
                end
                BOUNDS: begin end_offset<=last_offset+{16'd0,req_width}; state<=CHECK; end
                CHECK: begin
                    if(use_slot && !fill_slot && valid_slots[bank] &&
                       req_width!=0 && req_width<=1024 && req_first_row<1024 &&
                       req_rows!=0 && req_width<=pitch && end_offset<=counts[bank]) begin
                        row_base<=first_offset; pixel_offset<=first_offset;
                        row_left<=req_width; rows_left<=req_rows; state<=RAM_READ;
                    end else state<=LAUNCH; // Empty/out-of-range slot: normal DDR read.
                end
                LAUNCH: state<=DDR;
                DDR: begin
                    if(ddr_done) begin ddr_finished<=1; failed<=ddr_error; end
                    if(fill_slot && in_valid && counts[bank]<1024) begin
                        capture_data<=in_data; capture_keep<=in_keep;
                    end
                    if(capture_keep!=0) begin
                        capture_keep<=capture_keep>>1;
                        capture_data<=capture_data>>16;
                        if(capture_keep[0] && counts[bank]<1024) begin
                            pack_word[bank]<=packed_next;
                            counts[bank]<=counts[bank]+1'b1;
                            if(counts[bank][1:0]==3) begin
                                ram_we<=1; write_address<=counts[bank][9:2]; write_data<=packed_next;
                            end
                        end
                    end
                    if(ddr_finished && capture_keep==0) state<=FLUSH;
                end
                FLUSH: begin
                    if(fill_slot && counts[bank][1:0]!=0) begin
                        ram_we<=1; write_address<=counts[bank][9:2]; write_data<=pack_word[bank];
                    end
                    state<=COMMIT;
                end
                COMMIT: begin
                    if(fill_slot) begin
                        if(failed) valid_slots[bank]<=0;
                        else if(final_batch || counts[bank]==1024) valid_slots[bank]<=1;
                    end
                    state<=FINISH;
                end
                RAM_READ: begin
                    // One synchronous RAM response per cycle. Its output and
                    // the matching lane/keep tags hold together under stall.
                    if(replay_valid && !fifo_full) begin
                        replay_valid<=0;
                        if(replay_last) state<=FINISH;
                    end
                    if(ram_re) begin
                        replay_valid<=1;
                        replay_lane<=pixel_offset[1:0];
                        replay_keep<={(next_pixels>=4),(next_pixels>=3),(next_pixels>=2),(next_pixels>=1)};
                        replay_last<=rows_left==1 && row_left==next_pixels;
                        if(row_left==next_pixels) begin
                            if(rows_left==1) issued_all<=1;
                            else begin
                                rows_left<=rows_left-1'b1; row_left<=req_width;
                                row_base<=row_base+pitch; pixel_offset<=row_base+pitch;
                            end
                        end else begin
                            row_left<=row_left-next_pixels; pixel_offset<=pixel_offset+next_pixels;
                        end
                    end
                end
                FINISH: state<=IDLE;
                default: state<=IDLE;
            endcase
        end
    end
endmodule
