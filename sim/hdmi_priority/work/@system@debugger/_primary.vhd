library verilog;
use verilog.vl_types.all;
entity SystemDebugger is
    port(
        io_remote_cmd_valid: in     vl_logic;
        io_remote_cmd_ready: out    vl_logic;
        io_remote_cmd_payload_last: in     vl_logic;
        io_remote_cmd_payload_fragment: in     vl_logic_vector(0 downto 0);
        io_remote_rsp_valid: out    vl_logic;
        io_remote_rsp_ready: in     vl_logic;
        io_remote_rsp_payload_error: out    vl_logic;
        io_remote_rsp_payload_data: out    vl_logic_vector(31 downto 0);
        io_mem_cmd_valid: out    vl_logic;
        io_mem_cmd_ready: in     vl_logic;
        io_mem_cmd_payload_address: out    vl_logic_vector(31 downto 0);
        io_mem_cmd_payload_data: out    vl_logic_vector(31 downto 0);
        io_mem_cmd_payload_wr: out    vl_logic;
        io_mem_cmd_payload_size: out    vl_logic_vector(1 downto 0);
        io_mem_rsp_valid: in     vl_logic;
        io_mem_rsp_payload: in     vl_logic_vector(31 downto 0);
        io_systemClk    : in     vl_logic;
        debugCd_logic_outputReset: in     vl_logic
    );
end SystemDebugger;
