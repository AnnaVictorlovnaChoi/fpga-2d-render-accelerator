library verilog;
use verilog.vl_types.all;
entity BmbI2cCtrl is
    port(
        io_ctrl_cmd_valid: in     vl_logic;
        io_ctrl_cmd_ready: out    vl_logic;
        io_ctrl_cmd_payload_last: in     vl_logic;
        io_ctrl_cmd_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_ctrl_cmd_payload_fragment_address: in     vl_logic_vector(7 downto 0);
        io_ctrl_cmd_payload_fragment_length: in     vl_logic_vector(1 downto 0);
        io_ctrl_cmd_payload_fragment_data: in     vl_logic_vector(31 downto 0);
        io_ctrl_cmd_payload_fragment_context: in     vl_logic_vector(3 downto 0);
        io_ctrl_rsp_valid: out    vl_logic;
        io_ctrl_rsp_ready: in     vl_logic;
        io_ctrl_rsp_payload_last: out    vl_logic;
        io_ctrl_rsp_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_ctrl_rsp_payload_fragment_data: out    vl_logic_vector(31 downto 0);
        io_ctrl_rsp_payload_fragment_context: out    vl_logic_vector(3 downto 0);
        io_i2c_sda_write: out    vl_logic;
        io_i2c_sda_read : in     vl_logic;
        io_i2c_scl_write: out    vl_logic;
        io_i2c_scl_read : in     vl_logic;
        io_interrupt    : out    vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end BmbI2cCtrl;
