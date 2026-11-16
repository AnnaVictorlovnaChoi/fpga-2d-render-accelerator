library verilog;
use verilog.vl_types.all;
entity BmbDecoder is
    port(
        io_input_cmd_valid: in     vl_logic;
        io_input_cmd_ready: out    vl_logic;
        io_input_cmd_payload_last: in     vl_logic;
        io_input_cmd_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_input_cmd_payload_fragment_address: in     vl_logic_vector(31 downto 0);
        io_input_cmd_payload_fragment_length: in     vl_logic_vector(1 downto 0);
        io_input_cmd_payload_fragment_data: in     vl_logic_vector(31 downto 0);
        io_input_cmd_payload_fragment_mask: in     vl_logic_vector(3 downto 0);
        io_input_rsp_valid: out    vl_logic;
        io_input_rsp_ready: in     vl_logic;
        io_input_rsp_payload_last: out    vl_logic;
        io_input_rsp_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_input_rsp_payload_fragment_data: out    vl_logic_vector(31 downto 0);
        io_outputs_0_cmd_valid: out    vl_logic;
        io_outputs_0_cmd_ready: in     vl_logic;
        io_outputs_0_cmd_payload_last: out    vl_logic;
        io_outputs_0_cmd_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_outputs_0_cmd_payload_fragment_address: out    vl_logic_vector(31 downto 0);
        io_outputs_0_cmd_payload_fragment_length: out    vl_logic_vector(1 downto 0);
        io_outputs_0_cmd_payload_fragment_data: out    vl_logic_vector(31 downto 0);
        io_outputs_0_cmd_payload_fragment_mask: out    vl_logic_vector(3 downto 0);
        io_outputs_0_rsp_valid: in     vl_logic;
        io_outputs_0_rsp_ready: out    vl_logic;
        io_outputs_0_rsp_payload_last: in     vl_logic;
        io_outputs_0_rsp_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_outputs_0_rsp_payload_fragment_data: in     vl_logic_vector(31 downto 0);
        io_systemClk    : in     vl_logic;
        debugCd_logic_outputReset: in     vl_logic
    );
end BmbDecoder;
