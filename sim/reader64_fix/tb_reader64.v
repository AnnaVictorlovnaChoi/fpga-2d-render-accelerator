`timescale 1ns/1ps
module tb_reader64;
reg clk=0; always #5 clk=~clk;
reg rst_n=0, cv=0;
reg [31:0] addr=0, length=0, stride=0;
reg [6:0] rows=0;
wire cr, done, error, fe, full, empty;
wire [63:0] fd;
wire [3:0] fk;
wire [15:0] pixel;
wire [31:0] araddr;
wire [7:0] arlen;
wire [2:0] arsize;
wire av, rr;
reg ready=0, rv=0, rl=0, pop=0, r_accepted=0;
reg [63:0] data=0;
integer cycle=0, qw=0, qr=0, bi=0, received=0, dones=0, stalls=0, max_pending=0;
integer qa[0:4095], qn[0:4095];
integer expected_addr, lane, a, w, j;
function [15:0] sample;
input integer address;
begin sample=((address/2)*37)^16'hc5a9; end
endfunction
axi_rgb565_reader dut(.clk(clk),.rst_n(rst_n),.cmd_valid(cv),.cmd_ready(cr),
.cmd_addr(addr),.cmd_length_bytes(length),.cmd_stride_bytes(stride),.cmd_rows(rows),
.done(done),.error(error),.pause_new_bursts(1'b0),.fifo_full(full),
.fifo_wr_en(fe),.fifo_wr_data(fd),.fifo_wr_keep(fk),
.m_axi_araddr(araddr),.m_axi_arlen(arlen),.m_axi_arsize(arsize),.m_axi_arvalid(av),.m_axi_arready(ready),
.m_axi_rid(4'd0),.m_axi_rdata(data),.m_axi_rresp(2'd0),.m_axi_rlast(rl),.m_axi_rvalid(rv),.m_axi_rready(rr));
gpu_sync_fifo #(.WR_WIDTH(64),.RD_WIDTH(16),.DEPTH(64),.PROG_FULL_LEVEL(56)) fifo(
.clk(clk),.rst_n(rst_n),.wr_en(fe),.wr_data(fd),.wr_keep(fk),.rd_en(pop),.rd_data(pixel),.full(full),.empty(empty));
always @(posedge clk) if(rst_n) begin
 r_accepted=rv && rr;
 cycle=cycle+1;
 if(av && ready) begin
  if(arsize!=3 || arlen>7 || araddr[2:0]!=0 || (araddr%4096)+(arlen+1)*8>4096) $fatal(1,"bad AR");
  qa[qw]=araddr; qn[qw]=arlen+1; qw=qw+1;
 end
 if(qw-qr>max_pending) max_pending=qw-qr;
 if(rv && !rr) stalls=stalls+1;
 if(rv && rr) begin
  if(rl) begin qr=qr+1; bi=0; end else bi=bi+1;
 end
 if(pop && !empty) begin
  expected_addr=addr+(received/(length/2))*stride+2*(received%(length/2));
  if(pixel!==sample(expected_addr)) $fatal(1,"pixel %0d addr %h got %h expected %h",received,expected_addr,pixel,sample(expected_addr));
  received=received+1;
 end
 if(done) dones=dones+1;
 if(error) $fatal(1,"reader error");
end
always @(negedge clk) if(rst_n) begin
 ready=(cycle%7!=0);
 pop=(cycle%5==0 || cycle%5==1);
 if(!rv || r_accepted) begin
  rv=0; rl=0;
  if(qr<qw && cycle%9!=0) begin
   rv=1; rl=(bi==qn[qr]-1);
   for(lane=0;lane<4;lane=lane+1) data[lane*16+:16]=sample(qa[qr]+bi*8+lane*2);
  end
 end
end
task run_case;
input integer base, width, height, pitch;
integer start_done, timeout;
begin
 @(negedge clk); #1;
 addr=base; length=width*2; rows=height; stride=pitch; received=0; start_done=dones; cv=1;
 @(negedge clk); #1; cv=0;
 timeout=0;
 while(received<width*height || dones==start_done) begin
  @(negedge clk); #1; timeout=timeout+1;
  if(timeout>100000) $fatal(1,"timeout got=%0d",received);
 end
 if(!empty || qw!=qr || received!=width*height) $fatal(1,"residue");
 $display("PASS base=%h width=%0d rows=%0d pitch=%0d pixels=%0d",base,width,height,pitch,received);
 repeat(3) @(negedge clk);
end
endtask
initial begin
 repeat(3) @(negedge clk); #1; rst_n=1;
 for(a=0;a<4;a=a+1) begin
  run_case(4096+a*2,32,32,454);
  for(w=1;w<=7;w=w+1) run_case(8192+a*2,w,9,454);
 end
 run_case(4090,65,4,258);
 run_case(8192,1280,1,2560);
 if(stalls==0 || max_pending<2) $fatal(1,"insufficient coverage");
 $display("PASS ALL reader64 pixel checks, R backpressure cycles=%0d max outstanding=%0d",stalls,max_pending);
 $finish;
end
initial begin #20000000; $fatal(1,"watchdog"); end
endmodule
