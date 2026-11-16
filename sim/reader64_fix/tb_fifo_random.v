`timescale 1ns/1ps
module fifo_check #(parameter RATIO=4)(output reg finished=0);
reg clk=0; always #5 clk=~clk;
reg rst_n=0,we=0,re=0;
reg [RATIO*16-1:0] wd=0;
reg [RATIO-1:0] keep=0;
wire [15:0] rd;
wire full,empty;
integer n,k,push=0,pop=0,seed=538+RATIO;
reg [15:0] expect_data[0:30000];
gpu_sync_fifo #(.WR_WIDTH(RATIO*16),.RD_WIDTH(16),.DEPTH(16)) dut(
.clk(clk),.rst_n(rst_n),.wr_en(we),.wr_data(wd),.wr_keep(keep),.rd_en(re),.rd_data(rd),.full(full),.empty(empty));
always @(posedge clk) if(rst_n) begin
 if(re && !empty) begin
  if(pop>=push || rd!==expect_data[pop]) $fatal(1,"ratio=%0d index=%0d got=%h expected=%h",RATIO,pop,rd,expect_data[pop]);
  pop=pop+1;
 end
 if(we && !full) for(k=0;k<RATIO;k=k+1) if(RATIO==1 || keep[k]) begin
  expect_data[push]=wd[k*16+:16]; push=push+1;
 end
end
initial begin
 repeat(3) @(negedge clk); rst_n=1;
 for(n=0;n<5000;n=n+1) begin
  @(negedge clk); we=($random(seed)&3)!=0; re=($random(seed)&3)!=0;
  keep=$random(seed);
  for(k=0;k<RATIO;k=k+1) wd[k*16+:16]=$random(seed);
 end
 @(negedge clk); we=0; re=1;
 repeat(30) @(negedge clk);
 if(!empty || pop!=push) $fatal(1,"not drained ratio=%0d",RATIO);
 $display("PASS randomized FIFO ratio=%0d pixels=%0d",RATIO,pop);
 finished=1;
end
endmodule
module tb_fifo_random;
wire a,b,c;
fifo_check #(.RATIO(1)) f1(a);
fifo_check #(.RATIO(2)) f2(b);
fifo_check #(.RATIO(4)) f4(c);
initial begin wait(a && b && c); $finish; end
initial begin #100000; $fatal(1,"watchdog"); end
endmodule
