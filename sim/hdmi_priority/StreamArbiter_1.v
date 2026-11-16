module StreamArbiter_1 #(parameter HDMI_PRIORITY = 0) (
  input  wire          io_inputs_0_valid,
  output wire          io_inputs_0_ready,
  input  wire [31:0]   io_inputs_0_payload_addr,
  input  wire [5:0]    io_inputs_0_payload_id,
  input  wire [3:0]    io_inputs_0_payload_region,
  input  wire [7:0]    io_inputs_0_payload_len,
  input  wire [2:0]    io_inputs_0_payload_size,
  input  wire [1:0]    io_inputs_0_payload_burst,
  input  wire [0:0]    io_inputs_0_payload_lock,
  input  wire [3:0]    io_inputs_0_payload_cache,
  input  wire [3:0]    io_inputs_0_payload_qos,
  input  wire [2:0]    io_inputs_0_payload_prot,
  input  wire          io_inputs_1_valid,
  output wire          io_inputs_1_ready,
  input  wire [31:0]   io_inputs_1_payload_addr,
  input  wire [5:0]    io_inputs_1_payload_id,
  input  wire [3:0]    io_inputs_1_payload_region,
  input  wire [7:0]    io_inputs_1_payload_len,
  input  wire [2:0]    io_inputs_1_payload_size,
  input  wire [1:0]    io_inputs_1_payload_burst,
  input  wire [0:0]    io_inputs_1_payload_lock,
  input  wire [3:0]    io_inputs_1_payload_cache,
  input  wire [3:0]    io_inputs_1_payload_qos,
  input  wire [2:0]    io_inputs_1_payload_prot,
  input  wire          io_inputs_2_valid,
  output wire          io_inputs_2_ready,
  input  wire [31:0]   io_inputs_2_payload_addr,
  input  wire [5:0]    io_inputs_2_payload_id,
  input  wire [3:0]    io_inputs_2_payload_region,
  input  wire [7:0]    io_inputs_2_payload_len,
  input  wire [2:0]    io_inputs_2_payload_size,
  input  wire [1:0]    io_inputs_2_payload_burst,
  input  wire [0:0]    io_inputs_2_payload_lock,
  input  wire [3:0]    io_inputs_2_payload_cache,
  input  wire [3:0]    io_inputs_2_payload_qos,
  input  wire [2:0]    io_inputs_2_payload_prot,
  output wire          io_output_valid,
  input  wire          io_output_ready,
  output wire [31:0]   io_output_payload_addr,
  output wire [5:0]    io_output_payload_id,
  output wire [3:0]    io_output_payload_region,
  output wire [7:0]    io_output_payload_len,
  output wire [2:0]    io_output_payload_size,
  output wire [1:0]    io_output_payload_burst,
  output wire [0:0]    io_output_payload_lock,
  output wire [3:0]    io_output_payload_cache,
  output wire [3:0]    io_output_payload_qos,
  output wire [2:0]    io_output_payload_prot,
  output wire [1:0]    io_chosen,
  output wire [2:0]    io_chosenOH,
  input  wire          io_memoryClk,
  input  wire          ddrCd_logic_outputReset
);

  wire       [5:0]    _zz__zz_maskProposal_0_2;
  wire       [5:0]    _zz__zz_maskProposal_0_2_1;
  wire       [2:0]    _zz__zz_maskProposal_0_2_2;
  reg        [31:0]   _zz_io_output_payload_addr_1;
  reg        [5:0]    _zz_io_output_payload_id;
  reg        [3:0]    _zz_io_output_payload_region;
  reg        [7:0]    _zz_io_output_payload_len;
  reg        [2:0]    _zz_io_output_payload_size;
  reg        [1:0]    _zz_io_output_payload_burst;
  reg        [0:0]    _zz_io_output_payload_lock;
  reg        [3:0]    _zz_io_output_payload_cache;
  reg        [3:0]    _zz_io_output_payload_qos;
  reg        [2:0]    _zz_io_output_payload_prot;
  // Read instance only: input 0 CPU, input 1 GPU, input 2 HDMI.
  // A stalled selection remains locked; priority applies to the next request.
  reg                 cpu_gpu_next; // 0: prefer CPU, 1: prefer GPU
  reg                 locked;
  wire                maskProposal_0;
  wire                maskProposal_1;
  wire                maskProposal_2;
  reg                 maskLocked_0;
  reg                 maskLocked_1;
  reg                 maskLocked_2;
  wire                maskRouted_0;
  wire                maskRouted_1;
  wire                maskRouted_2;
  wire       [2:0]    _zz_maskProposal_0;
  wire       [5:0]    _zz_maskProposal_0_1;
  wire       [5:0]    _zz_maskProposal_0_2;
  wire       [2:0]    _zz_maskProposal_0_3;
  wire                io_output_fire;
  wire       [1:0]    _zz_io_output_payload_addr;
  wire                _zz_io_chosen;
  wire                _zz_io_chosen_1;

  assign _zz__zz_maskProposal_0_2 = (_zz_maskProposal_0_1 - _zz__zz_maskProposal_0_2_1);
  assign _zz__zz_maskProposal_0_2_2 = {maskLocked_1,{maskLocked_0,maskLocked_2}};
  assign _zz__zz_maskProposal_0_2_1 = {3'd0, _zz__zz_maskProposal_0_2_2};
  always @(*) begin
    case(_zz_io_output_payload_addr)
      2'b00 : begin
        _zz_io_output_payload_addr_1 = io_inputs_0_payload_addr;
        _zz_io_output_payload_id = io_inputs_0_payload_id;
        _zz_io_output_payload_region = io_inputs_0_payload_region;
        _zz_io_output_payload_len = io_inputs_0_payload_len;
        _zz_io_output_payload_size = io_inputs_0_payload_size;
        _zz_io_output_payload_burst = io_inputs_0_payload_burst;
        _zz_io_output_payload_lock = io_inputs_0_payload_lock;
        _zz_io_output_payload_cache = io_inputs_0_payload_cache;
        _zz_io_output_payload_qos = io_inputs_0_payload_qos;
        _zz_io_output_payload_prot = io_inputs_0_payload_prot;
      end
      2'b01 : begin
        _zz_io_output_payload_addr_1 = io_inputs_1_payload_addr;
        _zz_io_output_payload_id = io_inputs_1_payload_id;
        _zz_io_output_payload_region = io_inputs_1_payload_region;
        _zz_io_output_payload_len = io_inputs_1_payload_len;
        _zz_io_output_payload_size = io_inputs_1_payload_size;
        _zz_io_output_payload_burst = io_inputs_1_payload_burst;
        _zz_io_output_payload_lock = io_inputs_1_payload_lock;
        _zz_io_output_payload_cache = io_inputs_1_payload_cache;
        _zz_io_output_payload_qos = io_inputs_1_payload_qos;
        _zz_io_output_payload_prot = io_inputs_1_payload_prot;
      end
      default : begin
        _zz_io_output_payload_addr_1 = io_inputs_2_payload_addr;
        _zz_io_output_payload_id = io_inputs_2_payload_id;
        _zz_io_output_payload_region = io_inputs_2_payload_region;
        _zz_io_output_payload_len = io_inputs_2_payload_len;
        _zz_io_output_payload_size = io_inputs_2_payload_size;
        _zz_io_output_payload_burst = io_inputs_2_payload_burst;
        _zz_io_output_payload_lock = io_inputs_2_payload_lock;
        _zz_io_output_payload_cache = io_inputs_2_payload_cache;
        _zz_io_output_payload_qos = io_inputs_2_payload_qos;
        _zz_io_output_payload_prot = io_inputs_2_payload_prot;
      end
    endcase
  end

  assign maskRouted_0 = (locked ? maskLocked_0 : maskProposal_0);
  assign maskRouted_1 = (locked ? maskLocked_1 : maskProposal_1);
  assign maskRouted_2 = (locked ? maskLocked_2 : maskProposal_2);
  assign _zz_maskProposal_0 = {io_inputs_2_valid,{io_inputs_1_valid,io_inputs_0_valid}};
  assign _zz_maskProposal_0_1 = {_zz_maskProposal_0,_zz_maskProposal_0};
  assign _zz_maskProposal_0_2 = (_zz_maskProposal_0_1 & (~ _zz__zz_maskProposal_0_2));
  assign _zz_maskProposal_0_3 = (_zz_maskProposal_0_2[5 : 3] | _zz_maskProposal_0_2[2 : 0]);
  assign maskProposal_0 = HDMI_PRIORITY ?
      (!io_inputs_2_valid && io_inputs_0_valid &&
       (!io_inputs_1_valid || !cpu_gpu_next)) : _zz_maskProposal_0_3[0];
  assign maskProposal_1 = HDMI_PRIORITY ?
      (!io_inputs_2_valid && io_inputs_1_valid &&
       (!io_inputs_0_valid || cpu_gpu_next)) : _zz_maskProposal_0_3[1];
  assign maskProposal_2 = HDMI_PRIORITY ? io_inputs_2_valid : _zz_maskProposal_0_3[2];
  always @(posedge io_memoryClk) begin
    if(ddrCd_logic_outputReset) cpu_gpu_next <= 1'b0;
    else if(io_output_fire && maskRouted_0) cpu_gpu_next <= 1'b1;
    else if(io_output_fire && maskRouted_1) cpu_gpu_next <= 1'b0;
  end
  assign io_output_fire = (io_output_valid && io_output_ready);
  assign io_output_valid = (((io_inputs_0_valid && maskRouted_0) || (io_inputs_1_valid && maskRouted_1)) || (io_inputs_2_valid && maskRouted_2));
  assign _zz_io_output_payload_addr = {maskRouted_2,maskRouted_1};
  assign io_output_payload_addr = _zz_io_output_payload_addr_1;
  assign io_output_payload_id = _zz_io_output_payload_id;
  assign io_output_payload_region = _zz_io_output_payload_region;
  assign io_output_payload_len = _zz_io_output_payload_len;
  assign io_output_payload_size = _zz_io_output_payload_size;
  assign io_output_payload_burst = _zz_io_output_payload_burst;
  assign io_output_payload_lock = _zz_io_output_payload_lock;
  assign io_output_payload_cache = _zz_io_output_payload_cache;
  assign io_output_payload_qos = _zz_io_output_payload_qos;
  assign io_output_payload_prot = _zz_io_output_payload_prot;
  assign io_inputs_0_ready = (maskRouted_0 && io_output_ready);
  assign io_inputs_1_ready = (maskRouted_1 && io_output_ready);
  assign io_inputs_2_ready = (maskRouted_2 && io_output_ready);
  assign io_chosenOH = {maskRouted_2,{maskRouted_1,maskRouted_0}};
  assign _zz_io_chosen = io_chosenOH[1];
  assign _zz_io_chosen_1 = io_chosenOH[2];
  assign io_chosen = {_zz_io_chosen_1,_zz_io_chosen};
  always @(posedge io_memoryClk) begin
    if(ddrCd_logic_outputReset) begin
      locked <= 1'b0;
      maskLocked_0 <= 1'b0;
      maskLocked_1 <= 1'b0;
      maskLocked_2 <= 1'b1;
    end else begin
      if(io_output_valid) begin
        maskLocked_0 <= maskRouted_0;
        maskLocked_1 <= maskRouted_1;
        maskLocked_2 <= maskRouted_2;
      end
      if(io_output_valid) begin
        locked <= 1'b1;
      end
      if(io_output_fire) begin
        locked <= 1'b0;
      end
    end
  end


endmodule
