library verilog;
use verilog.vl_types.all;
entity JtagBridgeNoTap is
    port(
        io_ctrl_tdi     : in     vl_logic;
        io_ctrl_enable  : in     vl_logic;
        io_ctrl_capture : in     vl_logic;
        io_ctrl_shift   : in     vl_logic;
        io_ctrl_update  : in     vl_logic;
        io_ctrl_reset   : in     vl_logic;
        io_ctrl_tdo     : out    vl_logic;
        io_remote_cmd_valid: out    vl_logic;
        io_remote_cmd_ready: in     vl_logic;
        io_remote_cmd_payload_last: out    vl_logic;
        io_remote_cmd_payload_fragment: out    vl_logic_vector(0 downto 0);
        io_remote_rsp_valid: in     vl_logic;
        io_remote_rsp_ready: out    vl_logic;
        io_remote_rsp_payload_error: in     vl_logic;
        io_remote_rsp_payload_data: in     vl_logic_vector(31 downto 0);
        io_systemClk    : in     vl_logic;
        debugCd_logic_outputReset: in     vl_logic;
        jtagCtrl_tck    : in     vl_logic
    );
end JtagBridgeNoTap;
