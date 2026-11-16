`timescale 1ns/1ps
module tb_gpu_cache;
reg clk=0; always #5 clk=~clk;
reg reset=1, cpu_av=0,cpu_wv=0;
reg [31:0] cpu_data=0;
wire cpu_ar,cpu_wr,cpu_bv; wire [1:0] cpu_resp;
wire av,rr,awv,wv,wl,br;
wire [31:0] aa,awa; wire [7:0] alen,awlen; wire [2:0] asz,awsz;
wire [63:0] wdata; wire [7:0] wstrb;
reg ardy=0,rv=0,rl=0,awready=0,wr=0,bv=0;
reg [63:0] rdata=0;
reg [7:0] mem[0:262143];
integer replay_run=0,replay_max=0;
integer cycle=0,qw=0,qr=0,bi=0,source_ar=0,write_addr=0,write_left=0,bdelay=0;
integer qa[0:16383],qn[0:16383];
integer i,lane,j,n,ar_before,read_accept=0,slot,overlap=0;
reg [15:0] wanted,actual,srcpixel;
GPU_top #(.CORE_COUNT(4)) dut(
 .user_clk(clk),
 .io_ddrMasters_1_reset(reset),
 .axiA_awid('0),
 .axiA_awaddr('0),
 .axiA_awlen('0),
 .axiA_awsize(3'd2),
 .axiA_awburst(2'd1),
 .axiA_awcache('0),
 .axiA_awqos('0),
 .axiA_awregion('0),
 .axiA_awprot('0),
 .axiA_awlock('0),
 .axiA_awvalid(cpu_av),
 .axiA_awready(cpu_ar),
 .axiA_wdata(cpu_data),
 .axiA_wstrb(4'hf),
 .axiA_wlast(1'b1),
 .axiA_wvalid(cpu_wv),
 .axiA_wready(cpu_wr),
 .axiA_bid(),
 .axiA_bresp(cpu_resp),
 .axiA_bvalid(cpu_bv),
 .axiA_bready(1'b1),
 .axiA_arid('0),
 .axiA_araddr('0),
 .axiA_arlen('0),
 .axiA_arsize('0),
 .axiA_arburst('0),
 .axiA_arcache('0),
 .axiA_arqos('0),
 .axiA_arregion('0),
 .axiA_arprot('0),
 .axiA_arlock('0),
 .axiA_arvalid('0),
 .axiA_arready(),
 .axiA_rid(),
 .axiA_rdata(),
 .axiA_rresp(),
 .axiA_rlast(),
 .axiA_rvalid(),
 .axiA_rready('0),
 .io_ddrMasters_1_ar_payload_id(),
 .io_ddrMasters_1_ar_payload_addr(aa),
 .io_ddrMasters_1_ar_payload_len(alen),
 .io_ddrMasters_1_ar_payload_size(asz),
 .io_ddrMasters_1_ar_payload_burst(),
 .io_ddrMasters_1_ar_payload_cache(),
 .io_ddrMasters_1_ar_payload_qos(),
 .io_ddrMasters_1_ar_payload_region(),
 .io_ddrMasters_1_ar_payload_prot(),
 .io_ddrMasters_1_ar_payload_lock(),
 .io_ddrMasters_1_ar_valid(av),
 .io_ddrMasters_1_ar_ready(ardy),
 .io_ddrMasters_1_r_payload_id('0),
 .io_ddrMasters_1_r_payload_data(rdata),
 .io_ddrMasters_1_r_payload_resp('0),
 .io_ddrMasters_1_r_payload_last(rl),
 .io_ddrMasters_1_r_valid(rv),
 .io_ddrMasters_1_r_ready(rr),
 .io_ddrMasters_1_aw_payload_id(),
 .io_ddrMasters_1_aw_payload_addr(awa),
 .io_ddrMasters_1_aw_payload_len(awlen),
 .io_ddrMasters_1_aw_payload_size(awsz),
 .io_ddrMasters_1_aw_payload_burst(),
 .io_ddrMasters_1_aw_payload_cache(),
 .io_ddrMasters_1_aw_payload_qos(),
 .io_ddrMasters_1_aw_payload_region(),
 .io_ddrMasters_1_aw_payload_prot(),
 .io_ddrMasters_1_aw_payload_lock(),
 .io_ddrMasters_1_aw_valid(awv),
 .io_ddrMasters_1_aw_ready(awready),
 .io_ddrMasters_1_w_payload_data(wdata),
 .io_ddrMasters_1_w_payload_strb(wstrb),
 .io_ddrMasters_1_w_payload_last(wl),
 .io_ddrMasters_1_w_valid(wv),
 .io_ddrMasters_1_w_ready(wr),
 .io_ddrMasters_1_b_payload_id('0),
 .io_ddrMasters_1_b_payload_resp('0),
 .io_ddrMasters_1_b_valid(bv),
 .io_ddrMasters_1_b_ready(br));
always @(posedge clk) if(!reset) begin

 
 if((dut.parser_busy & (dut.parser_busy-1))!=0) overlap=overlap+1;
 if(dut.dma8.source_cache.state==8 && dut.dma8.cache_out_valid) begin
  replay_run=replay_run+1;if(replay_run>replay_max) replay_max=replay_run;
 end else replay_run=0;
 cycle=cycle+1; read_accept=rv && rr;
 if(av && ardy) begin
  if(asz!=3 || alen>7 || aa[2:0]!=0) $fatal(1,"AR format");
  qa[qw]=aa; qn[qw]=alen+1; qw=qw+1;
  if(aa<65536) source_ar=source_ar+1;
 end
 if(read_accept) begin if(rl) begin qr=qr+1;bi=0;end else bi=bi+1;end
 if(awv && awready) begin
  if(write_left!=0 || awsz!=3 || awlen>7) $fatal(1,"AW format");
  write_addr=awa;write_left=awlen+1;
 end
 if(wv && wr) begin
  if(write_left==0 || wl!=(write_left==1)) $fatal(1,"W protocol");
  for(lane=0;lane<8;lane=lane+1) if(wstrb[lane]) mem[write_addr+lane]=wdata[lane*8+:8];
  write_addr=write_addr+8;write_left=write_left-1;
  if(wl) bdelay=4;
 end
 if(bv && br) bv=0;
 if(bdelay>0) begin bdelay=bdelay-1;if(bdelay==0) bv=1;end
 if(|dut.parser_error) $fatal(1,"parser error");
end
always @(negedge clk) if(!reset) begin
 ardy=cycle%7!=0; awready=(write_left==0 && !bv && bdelay==0 && cycle%5!=0);
 wr=write_left!=0 && cycle%4!=0;
 if(!rv || read_accept) begin
  rv=0;rl=0;
  if(qr<qw && cycle%6!=0) begin
   rv=1;rl=bi==qn[qr]-1;
   for(j=0;j<8;j=j+1) rdata[j*8+:8]=mem[qa[qr]+bi*8+j];
  end
 end
end
task word;
 input [31:0] v;
 reg aok,wok;
 begin
  @(negedge clk);#1;cpu_av=1;cpu_wv=1;cpu_data=v;aok=0;wok=0;
  while(!aok || !wok) begin
   @(posedge clk); if(cpu_av && cpu_ar) aok=1;if(cpu_wv && cpu_wr) wok=1;
   @(negedge clk);#1;if(aok) cpu_av=0;if(wok) cpu_wv=0;
  end
  wait(cpu_bv);if(cpu_resp!=0) $fatal(1,"CPU response");
  @(negedge clk);#1;
 end
endtask
task command;
 input integer op,source,dest,width,height,pitch;
 input [31:0] color;
 begin
  word((op<<8)|1);word(source);word(dest);word((height<<16)|width);
  word(pitch);word(256);word(color);
 end
endtask
task drain;
 integer timeout;
 begin
  timeout=0;
  repeat(3) @(negedge clk);
  while(dut.sched_busy || (|dut.sched_valid) || (|dut.parser_busy) || !dut.cmd_empty) begin
   @(negedge clk);timeout=timeout+1;
   if(timeout>200000) $fatal(1,"timeout cache state=%d owner=%d parser=%b",dut.dma8.source_cache.state,dut.dma8.read_owner,dut.parser_busy);
  end
  repeat(5) @(negedge clk);
 end
endtask
function [15:0] getpixel;
 input integer address;
 begin getpixel={mem[address+1],mem[address]};end
endfunction
function [15:0] blend;
 input [15:0] s,d;
 integer r,g,b;
 begin
  r=(s[15:11]*16+d[15:11]*16)/32;
  g=(s[10:5]*16+d[10:5]*16)/32;
  b=(s[4:0]*16+d[4:0]*16)/32;
  blend={r[4:0],g[5:0],b[4:0]};
 end
endfunction
task check;
 input integer source,dest,width,height,pitch,op;
 integer x,y;
 reg [15:0] sp,exp,got;
 begin
  for(y=0;y<height;y=y+1) for(x=0;x<width;x=x+1) begin
   sp=getpixel(source+y*pitch+x*2);
   exp=(op==1)?16'h1234:(op==2 && sp==0)?16'h5555:
       (op==3)?blend(sp,16'h5555):(op==5 && sp==0)?16'h5555:
       (op==5)?blend(sp,16'h5555):sp;
   got=getpixel(dest+y*256+x*2);
   if(got!==exp) $fatal(1,"op=%0d dest=%h at x=%0d y=%0d got=%h exp=%h",op,dest,x,y,got,exp);
  end
 end
endtask
initial begin
 for(i=0;i<262144;i=i+1) mem[i]=8'h55;
 for(i=0;i<32768;i=i+1) begin
  srcpixel=(i%9==0)?0:((i*37)^16'h6789);
  mem[2*i]=srcpixel[7:0];mem[2*i+1]=srcpixel[15:8];
 end
 repeat(5) @(negedge clk);#1;reset=0;
 // Empty slot must fetch DDR normally.
 ar_before=source_ar;
 command(0,4096,245760,5,3,64,32'h10000000);drain;
 check(4096,245760,5,3,64,0);
 if(source_ar==ar_before) $fatal(1,"empty slot must fall back");
 // Fill followed immediately by same-slot crop, no software fence.
 command(0,4098,65536,20,20,454,32'h90000000);
 command(0,12288,73728,13,11,256,32'h10000000);
 drain;check(4098,65536,20,20,454,0);check(4098,73728,13,11,454,0);
 $display("PASS FCFS fill then crop");
 for(n=0;n<6;n=n+1) if(n==0 || n==2 || n==3 || n==5) begin
  ar_before=source_ar;
  command(n,12288,81920+n*4096,13,11,256,32'h10000010);
  drain;check(4098,81920+n*4096,13,11,454,n);
  if(source_ar!=ar_before) $fatal(1,"cache hit accessed DDR source");
 end
 $display("PASS BLT/sprite/alpha/alphasprite cached pixel results and zero source AR");
 // All seven slots are independent; awkward row tails must compact correctly.
 for(slot=2;slot<=7;slot=slot+1) begin
  command(0,slot*4096+6,114688,7,9,34,(32'h8+slot)<<28);drain;
  ar_before=source_ar;
  command(0,4096,118784,5,8,32,slot<<28);drain;
  check(slot*4096+6,118784,5,8,34,0);
  if(source_ar!=ar_before) $fatal(1,"slot missed");
 end
 $display("PASS all seven banks and unaligned partial rows");
 // 1025 pixels: second batch continues first 1000 and truncates at 1024.
 command(0,16384,131072,25,41,64,32'hc0000000);drain;
 check(16384,131072,25,41,64,0);
 if(dut.dma8.source_cache.counts[3]!=1024) $fatal(1,"cache overflow");
 ar_before=source_ar;
 command(0,4096,147456,24,41,64,32'h40000000);drain;
 check(16384,147456,24,41,64,0);
 if(source_ar!=ar_before) $fatal(1,"prefix crop missed");
 $display("PASS capped 1024 prefix across batches");
 // Width 1 spans more than the parser's 64-row batch limit.
 command(0,24576,163840,1,80,6,32'ha0000000);drain;
 ar_before=source_ar;
 command(0,4096,188416,1,80,6,32'h20000000);drain;
 check(24576,188416,1,80,6,0);
 if(source_ar!=ar_before) $fatal(1,"multi batch cache missed");
 // A too-large replay must safely fall back to DDR.
 ar_before=source_ar;
 command(0,4096,212992,26,2,64,32'h40000000);drain;
 check(4096,212992,26,2,64,0);
 if(source_ar==ar_before) $fatal(1,"out of bounds not bypassed");
 for(slot=0;slot<8;slot=slot+1) if(slot==0) begin
  ar_before=source_ar;
  command(0,4096,221184,7,3,64,slot<<28|32'h80000000);drain;
  check(4096,221184,7,3,64,0);
  if(source_ar==ar_before) $fatal(1,"invalid slot not bypassed");
 end
 command(1,4096,229376,7,3,64,32'hf0001234);drain;check(4096,229376,7,3,64,1);
 $display("PASS multi batch, invalid IDs, bounds fallback, colorfill bypass");
 // Filling via blend/transparent instructions must store SOURCE, not result.
 for(n=2;n<=5;n=n+1) if(n!=4) begin
  for(i=237568;i<241664;i=i+1) mem[i]=8'h55;
  command(n,20482,237568,7,9,34,32'h90000010);drain;
  check(20482,237568,7,9,34,n);
  ar_before=source_ar;
  command(0,4096,241664,7,9,34,32'h10000000);drain;
  check(20482,241664,7,9,34,0);
  if(source_ar!=ar_before) $fatal(1,"blend fill not cached");
 end
 // Overwrite same slot twice and then replay: FCFS must preserve final image.
 command(0,4096,65536,20,20,64,32'h90000000);
 command(0,12288,73728,20,20,64,32'h90000000);
 command(0,20480,81920,20,20,64,32'h10000000);
 drain;check(4096,65536,20,20,64,0);check(12288,73728,20,20,64,0);check(12288,81920,20,20,64,0);
 // Full 32x32 capacity and a single source row larger than the cache.
 command(0,8192,98304,32,32,64,32'hb0000000);drain;
 ar_before=source_ar;
 command(0,4096,114688,32,32,64,32'h30000000);drain;
 check(8192,114688,32,32,64,0);
 if(source_ar!=ar_before) $fatal(1,"32x32 cache missed");
 command(0,4096,229376,1280,1,2560,32'hb0000000);drain;
 check(4096,229376,1280,1,2560,0);
 ar_before=source_ar;
 command(0,8192,233472,1024,1,2560,32'h30000000);drain;
 check(4096,233472,1024,1,2560,0);
 if(source_ar!=ar_before || dut.dma8.source_cache.counts[2]!=1024) $fatal(1,"large row truncation");
 $display("PASS 32x32 full slot and 1280-pixel row prefix");
 // OP=4 keeps its original scale path and ignores cache bits.
 command(4,4096,249856,7,3,64,32'h90000040);drain;
 // Preserve the existing parser OP=4 row-address convention (scale_q26*stride).
 check(4096,249856,7,3,4096,0);
 $display("PASS blend source fill, consecutive overwrites, OP=4 regression");
 if(overlap==0) $fatal(1,"no multi core overlap");
 $display("PASS multi core overlap cycles=%0d",overlap);
 if(replay_max<8) $fatal(1,"cache did not stream one word per cycle");
 $display("PASS cache streaming max consecutive 64-bit outputs=%0d",replay_max);
 $display("PASS ALL GPU cache integration checks");$finish;
end
// Stall a live cache response: RAM word, lane and keep must stay aligned.
initial begin
 wait(!reset);
 wait(dut.dma8.source_cache.state==8 && dut.dma8.source_cache.replay_valid);
 @(negedge clk); force dut.fifo_full=4'b1111;
 repeat(13) @(negedge clk);
 release dut.fifo_full;
end
initial begin #30000000;$fatal(1,"global watchdog");end
endmodule
