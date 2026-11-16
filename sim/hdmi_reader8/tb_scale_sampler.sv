`timescale 1ns/1ps
module tb_scale_sampler;
  reg clk=0; always #5 clk=~clk;
  reg rst_n=0, enable=1, line_start=0, src_empty=1, out_ready=1;
  reg [7:0] scale=8'd64;
  reg [15:0] dst_width=32;
  reg [15:0] src_data=0;
  wire src_rd, out_valid, line_done;
  wire [15:0] out_data;
  integer reads=0, outputs=0, cycles=0;
  gpu_scale_x_sampler dut(.clk(clk),.rst_n(rst_n),.enable(enable),
    .line_start(line_start),.scale_q26(scale),.dst_width(dst_width),
    .src_fifo_empty(src_empty),.src_fifo_data(src_data),.src_fifo_rd_en(src_rd),
    .out_valid(out_valid),.out_ready(out_ready),.out_data(out_data),.line_done(line_done));
  always @(posedge clk) begin
    cycles<=cycles+1;
    if(src_rd) begin reads<=reads+1;src_data<=src_data+1;end
  end
  always @(negedge clk) if(rst_n && out_valid && out_ready) outputs=outputs+1;
  initial begin
    repeat(3) @(negedge clk);rst_n=1;
    @(negedge clk);src_empty=0;line_start=1;
    @(negedge clk);line_start=0;
    wait(line_done);@(negedge clk);
    $display("scale=%0d width=%0d reads=%0d outputs=%0d cycles=%0d",scale,dst_width,reads,outputs,cycles);
    if(outputs!=dst_width) $fatal(1,"wrong output count");
    $finish;
  end
  initial begin #100000;$fatal(1,"sampler hang: read=%0d out=%0d src=%0d wanted=%0d valid=%b",reads,outputs,dut.src_index,dut.wanted_index,dut.current_valid);end
endmodule
