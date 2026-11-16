library verilog;
use verilog.vl_types.all;
entity TopLevel is
    port(
        io_config_kind_cpol: in     vl_logic;
        io_config_kind_cpha: in     vl_logic;
        io_config_sclkToggle: in     vl_logic_vector(11 downto 0);
        io_config_mod   : in     vl_logic_vector(1 downto 0);
        io_config_ss_activeHigh: in     vl_logic_vector(0 downto 0);
        io_config_ss_setup: in     vl_logic_vector(11 downto 0);
        io_config_ss_hold: in     vl_logic_vector(11 downto 0);
        io_config_ss_disable: in     vl_logic_vector(11 downto 0);
        io_cmd_valid    : in     vl_logic;
        io_cmd_ready    : out    vl_logic;
        io_cmd_payload_kind: in     vl_logic;
        io_cmd_payload_read: in     vl_logic;
        io_cmd_payload_write: in     vl_logic;
        io_cmd_payload_data: in     vl_logic_vector(7 downto 0);
        io_rsp_valid    : out    vl_logic;
        io_rsp_payload_data: out    vl_logic_vector(7 downto 0);
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
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end TopLevel;
