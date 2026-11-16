library verilog;
use verilog.vl_types.all;
entity I2cSlave is
    port(
        io_i2c_sda_write: out    vl_logic;
        io_i2c_sda_read : in     vl_logic;
        io_i2c_scl_write: out    vl_logic;
        io_i2c_scl_read : in     vl_logic;
        io_config_samplingClockDivider: in     vl_logic_vector(9 downto 0);
        io_config_timeout: in     vl_logic_vector(19 downto 0);
        io_config_tsuData: in     vl_logic_vector(5 downto 0);
        io_config_timeoutClear: in     vl_logic;
        io_bus_cmd_kind : out    vl_logic_vector(2 downto 0);
        io_bus_cmd_data : out    vl_logic;
        io_bus_rsp_valid: in     vl_logic;
        io_bus_rsp_enable: in     vl_logic;
        io_bus_rsp_data : in     vl_logic;
        io_timeout      : out    vl_logic;
        io_internals_inFrame: out    vl_logic;
        io_internals_sdaRead: out    vl_logic;
        io_internals_sclRead: out    vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end I2cSlave;
