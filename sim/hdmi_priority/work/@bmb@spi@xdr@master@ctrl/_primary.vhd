library verilog;
use verilog.vl_types.all;
entity BmbSpiXdrMasterCtrl is
    port(
        io_ctrl_cmd_valid: in     vl_logic;
        io_ctrl_cmd_ready: out    vl_logic;
        io_ctrl_cmd_payload_last: in     vl_logic;
        io_ctrl_cmd_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_ctrl_cmd_payload_fragment_address: in     vl_logic_vector(11 downto 0);
        io_ctrl_cmd_payload_fragment_length: in     vl_logic_vector(1 downto 0);
        io_ctrl_cmd_payload_fragment_data: in     vl_logic_vector(31 downto 0);
        io_ctrl_cmd_payload_fragment_context: in     vl_logic_vector(3 downto 0);
        io_ctrl_rsp_valid: out    vl_logic;
        io_ctrl_rsp_ready: in     vl_logic;
        io_ctrl_rsp_payload_last: out    vl_logic;
        io_ctrl_rsp_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_ctrl_rsp_payload_fragment_data: out    vl_logic_vector(31 downto 0);
        io_ctrl_rsp_payload_fragment_context: out    vl_logic_vector(3 downto 0);
        io_spi_sclk_write: out    vl_logic_vector(0 downto 0);
        io_spi_data_0_writeEnable: out    vl_logic;
        io_spi_data_0_read: in     vl_logic_vector(0 downto 0);
        io_spi_data_0_write: out    vl_logic_vector(0 downto 0);
        io_spi_data_1_writeEnable: out    vl_logic;
        io_spi_data_1_read: in     vl_logic_vector(0 downto 0);
        io_spi_data_1_write: out    vl_logic_vector(0 downto 0);
        io_spi_data_2_writeEnable: out    vl_logic;
        io_spi_data_2_read: in     vl_logic_vector(0 downto 0);
        io_spi_data_2_write: out    vl_logic_vector(0 downto 0);
        io_spi_data_3_writeEnable: out    vl_logic;
        io_spi_data_3_read: in     vl_logic_vector(0 downto 0);
        io_spi_data_3_write: out    vl_logic_vector(0 downto 0);
        io_spi_ss       : out    vl_logic_vector(0 downto 0);
        io_interrupt    : out    vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end BmbSpiXdrMasterCtrl;
