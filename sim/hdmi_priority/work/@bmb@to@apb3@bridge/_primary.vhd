library verilog;
use verilog.vl_types.all;
entity BmbToApb3Bridge is
    port(
        io_input_cmd_valid: in     vl_logic;
        io_input_cmd_ready: out    vl_logic;
        io_input_cmd_payload_last: in     vl_logic;
        io_input_cmd_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_input_cmd_payload_fragment_address: in     vl_logic_vector(15 downto 0);
        io_input_cmd_payload_fragment_length: in     vl_logic_vector(1 downto 0);
        io_input_cmd_payload_fragment_data: in     vl_logic_vector(31 downto 0);
        io_input_cmd_payload_fragment_context: in     vl_logic_vector(3 downto 0);
        io_input_rsp_valid: out    vl_logic;
        io_input_rsp_ready: in     vl_logic;
        io_input_rsp_payload_last: out    vl_logic;
        io_input_rsp_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_input_rsp_payload_fragment_data: out    vl_logic_vector(31 downto 0);
        io_input_rsp_payload_fragment_context: out    vl_logic_vector(3 downto 0);
        io_output_PADDR : out    vl_logic_vector(15 downto 0);
        io_output_PSEL  : out    vl_logic_vector(0 downto 0);
        io_output_PENABLE: out    vl_logic;
        io_output_PREADY: in     vl_logic;
        io_output_PWRITE: out    vl_logic;
        io_output_PWDATA: out    vl_logic_vector(31 downto 0);
        io_output_PRDATA: in     vl_logic_vector(31 downto 0);
        io_output_PSLVERROR: in     vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end BmbToApb3Bridge;
