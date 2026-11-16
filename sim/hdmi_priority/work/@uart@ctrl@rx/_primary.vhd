library verilog;
use verilog.vl_types.all;
entity UartCtrlRx is
    port(
        io_configFrame_dataLength: in     vl_logic_vector(2 downto 0);
        io_configFrame_stop: in     vl_logic_vector(0 downto 0);
        io_configFrame_parity: in     vl_logic_vector(1 downto 0);
        io_samplingTick : in     vl_logic;
        io_read_valid   : out    vl_logic;
        io_read_ready   : in     vl_logic;
        io_read_payload : out    vl_logic_vector(7 downto 0);
        io_rxd          : in     vl_logic;
        io_rts          : out    vl_logic;
        io_error        : out    vl_logic;
        io_break        : out    vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end UartCtrlRx;
