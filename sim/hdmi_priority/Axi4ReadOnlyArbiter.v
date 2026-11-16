module Axi4ReadOnlyArbiter (
  input  wire          io_inputs_0_ar_valid,
  output wire          io_inputs_0_ar_ready,
  input  wire [31:0]   io_inputs_0_ar_payload_addr,
  input  wire [5:0]    io_inputs_0_ar_payload_id,
  input  wire [3:0]    io_inputs_0_ar_payload_region,
  input  wire [7:0]    io_inputs_0_ar_payload_len,
  input  wire [2:0]    io_inputs_0_ar_payload_size,
  input  wire [1:0]    io_inputs_0_ar_payload_burst,
  input  wire [0:0]    io_inputs_0_ar_payload_lock,
  input  wire [3:0]    io_inputs_0_ar_payload_cache,
  input  wire [3:0]    io_inputs_0_ar_payload_qos,
  input  wire [2:0]    io_inputs_0_ar_payload_prot,
  output wire          io_inputs_0_r_valid,
  input  wire          io_inputs_0_r_ready,
  output wire [127:0]  io_inputs_0_r_payload_data,
  output wire [5:0]    io_inputs_0_r_payload_id,
  output wire [1:0]    io_inputs_0_r_payload_resp,
  output wire          io_inputs_0_r_payload_last,
  input  wire          io_inputs_1_ar_valid,
  output wire          io_inputs_1_ar_ready,
  input  wire [31:0]   io_inputs_1_ar_payload_addr,
  input  wire [5:0]    io_inputs_1_ar_payload_id,
  input  wire [3:0]    io_inputs_1_ar_payload_region,
  input  wire [7:0]    io_inputs_1_ar_payload_len,
  input  wire [2:0]    io_inputs_1_ar_payload_size,
  input  wire [1:0]    io_inputs_1_ar_payload_burst,
  input  wire [0:0]    io_inputs_1_ar_payload_lock,
  input  wire [3:0]    io_inputs_1_ar_payload_cache,
  input  wire [3:0]    io_inputs_1_ar_payload_qos,
  input  wire [2:0]    io_inputs_1_ar_payload_prot,
  output wire          io_inputs_1_r_valid,
  input  wire          io_inputs_1_r_ready,
  output wire [127:0]  io_inputs_1_r_payload_data,
  output wire [5:0]    io_inputs_1_r_payload_id,
  output wire [1:0]    io_inputs_1_r_payload_resp,
  output wire          io_inputs_1_r_payload_last,
  input  wire          io_inputs_2_ar_valid,
  output wire          io_inputs_2_ar_ready,
  input  wire [31:0]   io_inputs_2_ar_payload_addr,
  input  wire [5:0]    io_inputs_2_ar_payload_id,
  input  wire [3:0]    io_inputs_2_ar_payload_region,
  input  wire [7:0]    io_inputs_2_ar_payload_len,
  input  wire [2:0]    io_inputs_2_ar_payload_size,
  input  wire [1:0]    io_inputs_2_ar_payload_burst,
  input  wire [0:0]    io_inputs_2_ar_payload_lock,
  input  wire [3:0]    io_inputs_2_ar_payload_cache,
  input  wire [3:0]    io_inputs_2_ar_payload_qos,
  input  wire [2:0]    io_inputs_2_ar_payload_prot,
  output wire          io_inputs_2_r_valid,
  input  wire          io_inputs_2_r_ready,
  output wire [127:0]  io_inputs_2_r_payload_data,
  output wire [5:0]    io_inputs_2_r_payload_id,
  output wire [1:0]    io_inputs_2_r_payload_resp,
  output wire          io_inputs_2_r_payload_last,
  output wire          io_output_ar_valid,
  input  wire          io_output_ar_ready,
  output wire [31:0]   io_output_ar_payload_addr,
  output wire [7:0]    io_output_ar_payload_id,
  output wire [3:0]    io_output_ar_payload_region,
  output wire [7:0]    io_output_ar_payload_len,
  output wire [2:0]    io_output_ar_payload_size,
  output wire [1:0]    io_output_ar_payload_burst,
  output wire [0:0]    io_output_ar_payload_lock,
  output wire [3:0]    io_output_ar_payload_cache,
  output wire [3:0]    io_output_ar_payload_qos,
  output wire [2:0]    io_output_ar_payload_prot,
  input  wire          io_output_r_valid,
  output wire          io_output_r_ready,
  input  wire [127:0]  io_output_r_payload_data,
  input  wire [7:0]    io_output_r_payload_id,
  input  wire [1:0]    io_output_r_payload_resp,
  input  wire          io_output_r_payload_last,
  input  wire          io_memoryClk,
  input  wire          ddrCd_logic_outputReset
);

  wire                cmdArbiter_io_inputs_0_ready;
  wire                cmdArbiter_io_inputs_1_ready;
  wire                cmdArbiter_io_inputs_2_ready;
  wire                cmdArbiter_io_output_valid;
  wire       [31:0]   cmdArbiter_io_output_payload_addr;
  wire       [5:0]    cmdArbiter_io_output_payload_id;
  wire       [3:0]    cmdArbiter_io_output_payload_region;
  wire       [7:0]    cmdArbiter_io_output_payload_len;
  wire       [2:0]    cmdArbiter_io_output_payload_size;
  wire       [1:0]    cmdArbiter_io_output_payload_burst;
  wire       [0:0]    cmdArbiter_io_output_payload_lock;
  wire       [3:0]    cmdArbiter_io_output_payload_cache;
  wire       [3:0]    cmdArbiter_io_output_payload_qos;
  wire       [2:0]    cmdArbiter_io_output_payload_prot;
  wire       [1:0]    cmdArbiter_io_chosen;
  wire       [2:0]    cmdArbiter_io_chosenOH;
  reg                 _zz_io_output_r_ready;
  wire       [1:0]    readRspIndex;
  wire                readRspSels_0;
  wire                readRspSels_1;
  wire                readRspSels_2;

  StreamArbiter_1 #(.HDMI_PRIORITY(1)) cmdArbiter (
    .io_inputs_0_valid          (io_inputs_0_ar_valid                    ), //i
    .io_inputs_0_ready          (cmdArbiter_io_inputs_0_ready            ), //o
    .io_inputs_0_payload_addr   (io_inputs_0_ar_payload_addr[31:0]       ), //i
    .io_inputs_0_payload_id     (io_inputs_0_ar_payload_id[5:0]          ), //i
    .io_inputs_0_payload_region (io_inputs_0_ar_payload_region[3:0]      ), //i
    .io_inputs_0_payload_len    (io_inputs_0_ar_payload_len[7:0]         ), //i
    .io_inputs_0_payload_size   (io_inputs_0_ar_payload_size[2:0]        ), //i
    .io_inputs_0_payload_burst  (io_inputs_0_ar_payload_burst[1:0]       ), //i
    .io_inputs_0_payload_lock   (io_inputs_0_ar_payload_lock             ), //i
    .io_inputs_0_payload_cache  (io_inputs_0_ar_payload_cache[3:0]       ), //i
    .io_inputs_0_payload_qos    (io_inputs_0_ar_payload_qos[3:0]         ), //i
    .io_inputs_0_payload_prot   (io_inputs_0_ar_payload_prot[2:0]        ), //i
    .io_inputs_1_valid          (io_inputs_1_ar_valid                    ), //i
    .io_inputs_1_ready          (cmdArbiter_io_inputs_1_ready            ), //o
    .io_inputs_1_payload_addr   (io_inputs_1_ar_payload_addr[31:0]       ), //i
    .io_inputs_1_payload_id     (io_inputs_1_ar_payload_id[5:0]          ), //i
    .io_inputs_1_payload_region (io_inputs_1_ar_payload_region[3:0]      ), //i
    .io_inputs_1_payload_len    (io_inputs_1_ar_payload_len[7:0]         ), //i
    .io_inputs_1_payload_size   (io_inputs_1_ar_payload_size[2:0]        ), //i
    .io_inputs_1_payload_burst  (io_inputs_1_ar_payload_burst[1:0]       ), //i
    .io_inputs_1_payload_lock   (io_inputs_1_ar_payload_lock             ), //i
    .io_inputs_1_payload_cache  (io_inputs_1_ar_payload_cache[3:0]       ), //i
    .io_inputs_1_payload_qos    (io_inputs_1_ar_payload_qos[3:0]         ), //i
    .io_inputs_1_payload_prot   (io_inputs_1_ar_payload_prot[2:0]        ), //i
    .io_inputs_2_valid          (io_inputs_2_ar_valid                    ), //i
    .io_inputs_2_ready          (cmdArbiter_io_inputs_2_ready            ), //o
    .io_inputs_2_payload_addr   (io_inputs_2_ar_payload_addr[31:0]       ), //i
    .io_inputs_2_payload_id     (io_inputs_2_ar_payload_id[5:0]          ), //i
    .io_inputs_2_payload_region (io_inputs_2_ar_payload_region[3:0]      ), //i
    .io_inputs_2_payload_len    (io_inputs_2_ar_payload_len[7:0]         ), //i
    .io_inputs_2_payload_size   (io_inputs_2_ar_payload_size[2:0]        ), //i
    .io_inputs_2_payload_burst  (io_inputs_2_ar_payload_burst[1:0]       ), //i
    .io_inputs_2_payload_lock   (io_inputs_2_ar_payload_lock             ), //i
    .io_inputs_2_payload_cache  (io_inputs_2_ar_payload_cache[3:0]       ), //i
    .io_inputs_2_payload_qos    (io_inputs_2_ar_payload_qos[3:0]         ), //i
    .io_inputs_2_payload_prot   (io_inputs_2_ar_payload_prot[2:0]        ), //i
    .io_output_valid            (cmdArbiter_io_output_valid              ), //o
    .io_output_ready            (io_output_ar_ready                      ), //i
    .io_output_payload_addr     (cmdArbiter_io_output_payload_addr[31:0] ), //o
    .io_output_payload_id       (cmdArbiter_io_output_payload_id[5:0]    ), //o
    .io_output_payload_region   (cmdArbiter_io_output_payload_region[3:0]), //o
    .io_output_payload_len      (cmdArbiter_io_output_payload_len[7:0]   ), //o
    .io_output_payload_size     (cmdArbiter_io_output_payload_size[2:0]  ), //o
    .io_output_payload_burst    (cmdArbiter_io_output_payload_burst[1:0] ), //o
    .io_output_payload_lock     (cmdArbiter_io_output_payload_lock       ), //o
    .io_output_payload_cache    (cmdArbiter_io_output_payload_cache[3:0] ), //o
    .io_output_payload_qos      (cmdArbiter_io_output_payload_qos[3:0]   ), //o
    .io_output_payload_prot     (cmdArbiter_io_output_payload_prot[2:0]  ), //o
    .io_chosen                  (cmdArbiter_io_chosen[1:0]               ), //o
    .io_chosenOH                (cmdArbiter_io_chosenOH[2:0]             ), //o
    .io_memoryClk               (io_memoryClk                            ), //i
    .ddrCd_logic_outputReset    (ddrCd_logic_outputReset                 )  //i
  );
  always @(*) begin
    case(readRspIndex)
      2'b00 : _zz_io_output_r_ready = io_inputs_0_r_ready;
      2'b01 : _zz_io_output_r_ready = io_inputs_1_r_ready;
      default : _zz_io_output_r_ready = io_inputs_2_r_ready;
    endcase
  end

  assign io_inputs_0_ar_ready = cmdArbiter_io_inputs_0_ready;
  assign io_inputs_1_ar_ready = cmdArbiter_io_inputs_1_ready;
  assign io_inputs_2_ar_ready = cmdArbiter_io_inputs_2_ready;
  assign io_output_ar_valid = cmdArbiter_io_output_valid;
  assign io_output_ar_payload_addr = cmdArbiter_io_output_payload_addr;
  assign io_output_ar_payload_region = cmdArbiter_io_output_payload_region;
  assign io_output_ar_payload_len = cmdArbiter_io_output_payload_len;
  assign io_output_ar_payload_size = cmdArbiter_io_output_payload_size;
  assign io_output_ar_payload_burst = cmdArbiter_io_output_payload_burst;
  assign io_output_ar_payload_lock = cmdArbiter_io_output_payload_lock;
  assign io_output_ar_payload_cache = cmdArbiter_io_output_payload_cache;
  assign io_output_ar_payload_qos = cmdArbiter_io_output_payload_qos;
  assign io_output_ar_payload_prot = cmdArbiter_io_output_payload_prot;
  assign io_output_ar_payload_id = {cmdArbiter_io_chosen,cmdArbiter_io_output_payload_id};
  assign readRspIndex = io_output_r_payload_id[7 : 6];
  assign readRspSels_0 = (readRspIndex == 2'b00);
  assign readRspSels_1 = (readRspIndex == 2'b01);
  assign readRspSels_2 = (readRspIndex == 2'b10);
  assign io_inputs_0_r_valid = (io_output_r_valid && readRspSels_0);
  assign io_inputs_0_r_payload_data = io_output_r_payload_data;
  assign io_inputs_0_r_payload_resp = io_output_r_payload_resp;
  assign io_inputs_0_r_payload_last = io_output_r_payload_last;
  assign io_inputs_0_r_payload_id = io_output_r_payload_id[5 : 0];
  assign io_inputs_1_r_valid = (io_output_r_valid && readRspSels_1);
  assign io_inputs_1_r_payload_data = io_output_r_payload_data;
  assign io_inputs_1_r_payload_resp = io_output_r_payload_resp;
  assign io_inputs_1_r_payload_last = io_output_r_payload_last;
  assign io_inputs_1_r_payload_id = io_output_r_payload_id[5 : 0];
  assign io_inputs_2_r_valid = (io_output_r_valid && readRspSels_2);
  assign io_inputs_2_r_payload_data = io_output_r_payload_data;
  assign io_inputs_2_r_payload_resp = io_output_r_payload_resp;
  assign io_inputs_2_r_payload_last = io_output_r_payload_last;
  assign io_inputs_2_r_payload_id = io_output_r_payload_id[5 : 0];
  assign io_output_r_ready = _zz_io_output_r_ready;

endmodule
