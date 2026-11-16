`timescale 1ns/1ps
module tb;
reg clk=0;always #5 clk=~clk;
reg reset=1;reg [2:0] v=0;reg ready=0;wire ov;wire [2:0] ir,oh;wire [1:0] chosen;
wire [31:0] o_addr;
wire [5:0] o_id;
wire [3:0] o_region;
wire [7:0] o_len;
wire [2:0] o_size;
wire [1:0] o_burst;
wire [0:0] o_lock;
wire [3:0] o_cache;
wire [3:0] o_qos;
wire [2:0] o_prot;
StreamArbiter_1 #(.HDMI_PRIORITY(1)) dut(.io_memoryClk(clk),.ddrCd_logic_outputReset(reset),.io_output_valid(ov),.io_output_ready(ready),.io_chosen(chosen),.io_chosenOH(oh),.io_inputs_0_valid(v[0]),.io_inputs_0_ready(ir[0]),.io_inputs_0_payload_addr(32'd1),.io_inputs_0_payload_id(6'd1),.io_inputs_0_payload_region(4'd1),.io_inputs_0_payload_len(8'd1),.io_inputs_0_payload_size(3'd1),.io_inputs_0_payload_burst(2'd1),.io_inputs_0_payload_lock(1'd1),.io_inputs_0_payload_cache(4'd1),.io_inputs_0_payload_qos(4'd1),.io_inputs_0_payload_prot(3'd1),.io_inputs_1_valid(v[1]),.io_inputs_1_ready(ir[1]),.io_inputs_1_payload_addr(32'd2),.io_inputs_1_payload_id(6'd2),.io_inputs_1_payload_region(4'd2),.io_inputs_1_payload_len(8'd2),.io_inputs_1_payload_size(3'd2),.io_inputs_1_payload_burst(2'd2),.io_inputs_1_payload_lock(1'd0),.io_inputs_1_payload_cache(4'd2),.io_inputs_1_payload_qos(4'd2),.io_inputs_1_payload_prot(3'd2),.io_inputs_2_valid(v[2]),.io_inputs_2_ready(ir[2]),.io_inputs_2_payload_addr(32'd3),.io_inputs_2_payload_id(6'd3),.io_inputs_2_payload_region(4'd3),.io_inputs_2_payload_len(8'd3),.io_inputs_2_payload_size(3'd3),.io_inputs_2_payload_burst(2'd3),.io_inputs_2_payload_lock(1'd1),.io_inputs_2_payload_cache(4'd3),.io_inputs_2_payload_qos(4'd3),.io_inputs_2_payload_prot(3'd3),.io_output_payload_addr(o_addr),.io_output_payload_id(o_id),.io_output_payload_region(o_region),.io_output_payload_len(o_len),.io_output_payload_size(o_size),.io_output_payload_burst(o_burst),.io_output_payload_lock(o_lock),.io_output_payload_cache(o_cache),.io_output_payload_qos(o_qos),.io_output_payload_prot(o_prot));
integer k,sel,held=-1,pref=0,grants[0:2];reg [2:0] expected;
initial begin
 grants[0]=0;grants[1]=0;grants[2]=0;
 repeat(3) @(negedge clk);reset=0;
 for(k=0;k<10000;k=k+1) begin
  // Directed contention and stalled GPU followed by HDMI, then random requests.
  if(k<12) begin v=3'b011;ready=1;end
  else if(k==12) begin v=3'b010;ready=0;end
  else if(k<16) begin v=3'b111;ready=(k==15);end
  else if(k<24) begin v=3'b111;ready=1;end
  else begin v=$random;ready=$random;end
  if(held>=0) v[held]=1; // AXI producer must retain VALID during backpressure.
  sel=held;
  if(sel<0) begin
   if(v[2]) sel=2;
   else if(v[0] && v[1]) sel=pref;
   else if(v[0]) sel=0;
   else if(v[1]) sel=1;
  end
  expected=sel<0?0:(3'b001<<sel);
  #1;
  if(oh!==expected || ov!==(sel>=0) || ir!==(ready?expected:3'b000)) $fatal(1,"selection cycle %0d",k);
  if(sel>=0) begin
   if(chosen!==sel) $fatal(1,"chosen");
   if(o_addr !== (sel+1)) $fatal(1,"payload addr");
   if(o_id !== ((sel+1) % 64)) $fatal(1,"payload id");
   if(o_region !== ((sel+1) % 16)) $fatal(1,"payload region");
   if(o_len !== ((sel+1) % 256)) $fatal(1,"payload len");
   if(o_size !== ((sel+1) % 8)) $fatal(1,"payload size");
   if(o_burst !== ((sel+1) % 4)) $fatal(1,"payload burst");
   if(o_lock !== ((sel+1) % 2)) $fatal(1,"payload lock");
   if(o_cache !== ((sel+1) % 16)) $fatal(1,"payload cache");
   if(o_qos !== ((sel+1) % 16)) $fatal(1,"payload qos");
   if(o_prot !== ((sel+1) % 8)) $fatal(1,"payload prot");
  end
  @(posedge clk);
  if(sel>=0 && ready) begin
   grants[sel]=grants[sel]+1;
   if(sel==0) pref=1;else if(sel==1) pref=0;
   held=-1;
  end else held=sel;
  @(negedge clk);
 end
 $display("PASS 10000 cycles: HDMI priority, CPU/GPU round robin, backpressure lock, all payloads. grants=%0d/%0d/%0d",grants[0],grants[1],grants[2]);$finish;
end
endmodule

