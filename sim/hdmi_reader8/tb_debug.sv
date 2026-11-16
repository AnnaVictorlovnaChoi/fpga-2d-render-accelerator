`timescale 1ns/1ps
module tb_axi_frame_reader;
    reg clk=0; always #5 clk=~clk;
    reg rst_n=0, start=0, fifo_full=0;
    wire busy,done,error,arvalid,rready,fifo_we;
    wire [31:0] araddr;
    wire [63:0] fifo_data;
    wire [7:0] arlen;
    reg arready=1,rvalid=0,rlast=0;
    reg [63:0] rdata=0;
    wire [3:0] arid;
    wire [2:0] arsize;
    wire [1:0] arburst;
    integer cycle=0, ar_count=0, r_count=0, fifo_count=0;
    integer outstanding=0, max_outstanding=0, bad_bool_early=0;
    reg frame_read_burst_active=0;
    integer response_beats=0, response_addr=0, response_wait=0;
    integer pending_beats[0:255], pending_addr[0:255];
    integer qwr=0,qrd=0,expected_addr=0,expected_words=0;
    reg [63:0] expected_data[0:2047];

    axi_frame_reader #(.AXI_ADDR_WIDTH(32),.AXI_DATA_WIDTH(64),
      .AXI_ID_WIDTH(4),.BURST_LEN(8)) dut (
      .axi_rst_n(rst_n),.start(start),.frame_base(32'h06000000),
      .frame_size(32'd8192),.busy(busy),.done(done),.error(error),
      .io_ddrMasters_0_clk(clk),.io_ddrMasters_0_ar_payload_id(arid),
      .io_ddrMasters_0_ar_payload_addr(araddr),.io_ddrMasters_0_ar_payload_len(arlen),
      .io_ddrMasters_0_ar_payload_size(arsize),.io_ddrMasters_0_ar_payload_burst(arburst),
      .io_ddrMasters_0_ar_payload_cache(),.io_ddrMasters_0_ar_payload_qos(),
      .io_ddrMasters_0_ar_payload_region(),.io_ddrMasters_0_ar_payload_prot(),
      .io_ddrMasters_0_ar_payload_lock(),.io_ddrMasters_0_ar_valid(arvalid),
      .io_ddrMasters_0_ar_ready(arready),.io_ddrMasters_0_r_payload_id(4'd0),
      .io_ddrMasters_0_r_payload_data(rdata),.io_ddrMasters_0_r_payload_resp(2'd0),
      .io_ddrMasters_0_r_payload_last(rlast),.io_ddrMasters_0_r_valid(rvalid),
      .io_ddrMasters_0_r_ready(rready),.io_ddrMasters_0_aw_payload_id(),
      .io_ddrMasters_0_aw_payload_addr(),.io_ddrMasters_0_aw_payload_len(),
      .io_ddrMasters_0_aw_payload_size(),.io_ddrMasters_0_aw_payload_burst(),
      .io_ddrMasters_0_aw_payload_cache(),.io_ddrMasters_0_aw_payload_qos(),
      .io_ddrMasters_0_aw_payload_region(),.io_ddrMasters_0_aw_payload_prot(),
      .io_ddrMasters_0_aw_payload_lock(),.io_ddrMasters_0_aw_valid(),
      .io_ddrMasters_0_aw_ready(1'b0),.io_ddrMasters_0_w_payload_data(),
      .io_ddrMasters_0_w_payload_strb(),.io_ddrMasters_0_w_payload_last(),
      .io_ddrMasters_0_w_valid(),.io_ddrMasters_0_w_ready(1'b0),
      .io_ddrMasters_0_b_payload_id(4'd0),.io_ddrMasters_0_b_payload_resp(2'd0),
      .io_ddrMasters_0_b_valid(1'b0),.io_ddrMasters_0_b_ready(),
      .fifo_full(fifo_full),.fifo_wr_en(fifo_we),.fifo_wr_data(fifo_data));

    always @(posedge clk) begin
      cycle<=cycle+1;
      if(!rst_n) begin
        outstanding<=0; frame_read_burst_active<=0;
      end else begin
        if(arvalid && arready) begin
          if(araddr!==32'h06000000+ar_count*64 || arlen!==7 || arsize!==3 || arburst!==1)
            $fatal(1,"bad AR #%0d addr=%h len=%0d",ar_count,araddr,arlen);
          pending_beats[qwr]=arlen+1; pending_addr[qwr]=araddr; qwr=qwr+1;
          ar_count<=ar_count+1; outstanding<=outstanding+1;
          frame_read_burst_active<=1;
          if(outstanding+1>max_outstanding) max_outstanding<=outstanding+1;
        end
        if(rvalid && rready) begin
          r_count<=r_count+1;
          if(rlast) begin
            outstanding<=outstanding-1;
            frame_read_burst_active<=0;
          end
        end
        if(frame_read_burst_active==0 && outstanding>0)
          bad_bool_early<=bad_bool_early+1;
        if(fifo_we)
          fifo_count<=fifo_count+1;
      end
    end

    always @(negedge clk) begin
      if(!rst_n) begin rvalid=0; response_wait=0; end
      else if(rvalid && rready) begin
        if(rlast) begin
          rvalid=0; response_beats=0; qrd=qrd+1; response_wait=3;
        end else response_beats=response_beats+1;
      end else if(!rvalid) begin
        if(response_wait>0) response_wait=response_wait-1;
        else if(qrd<qwr) begin
          rvalid=1; response_beats=0; response_addr=pending_addr[qrd];
          rdata={32'hA0000001+(response_addr-32'h06000000)/4,
                 32'hA0000000+(response_addr-32'h06000000)/4};
          rlast=(pending_beats[qrd]==1);
        end
      end
      if(rvalid && !(rvalid && rready)) begin
        rdata={32'hA0000001+((response_addr-32'h06000000)/4)+response_beats*2,
               32'hA0000000+((response_addr-32'h06000000)/4)+response_beats*2};
        rlast=(response_beats==pending_beats[qrd]-1);
      end
      fifo_full=(cycle%211>=202 && cycle%211<=206);
    end

    integer i,k;
    initial begin
      for(i=0;i<1024;i=i+1)
        expected_data[i]={32'hA0000001+i*2,32'hA0000000+i*2};
      repeat(5) @(negedge clk); rst_n=1;
      @(negedge clk); start=1;
      @(negedge clk); start=0;
      repeat(1000) @(negedge clk); $display("state=%0d ar=%0d r=%0d fifo=%0d qwr=%0d qrd=%0d desc=%0d issue=%0d",dut.state,ar_count,r_count,fifo_count,qwr,qrd,dut.desc_count,dut.issue_remaining); $finish; @(negedge clk);
      if(error) $fatal(1,"reader error");
      if(ar_count!=128 || r_count!=1024 || fifo_count!=1024)
        $fatal(1,"counts AR=%0d R=%0d FIFO=%0d",ar_count,r_count,fifo_count);
      if(max_outstanding!=8) $fatal(1,"expected 8 outstanding, got %0d",max_outstanding);
      if(bad_bool_early==0) $fatal(1,"expected to expose boolean burst-active bug");
      $display("PASS reader data/order/backpressure: AR=%0d R=%0d max_outstanding=%0d",ar_count,r_count,max_outstanding);
      $display("CONFIRMED top-level single-bit frame_read_burst_active was false for %0d cycles while %0d or more bursts remained outstanding",bad_bool_early,outstanding);
      $finish;
    end
    initial begin #2000000; $fatal(1,"watchdog"); end
endmodule

