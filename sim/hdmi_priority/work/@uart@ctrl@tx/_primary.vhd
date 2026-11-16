library verilog;
use verilog.vl_types.all;
entity UartCtrlTx is
    port(
        io_configFrame_dataLength: in     vl_logic_vector(2 downto 0);
        io_configFrame_stop: in     vl_logic_vector(0 downto 0);
        io_configFrame_parity: in     vl_logic_vector(1 downto 0);
        io_samplingTick : in     vl_logic;
        io_write_valid  : in     vl_logic;
        io_write_ready  : out    vl_logic;
        io_write_payload: in     vl_logic_vector(7 downto 0);
        io_cts          : in     vl_logic;
        io_txd          : out    vl_logic;
        io_break        : in     vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end UartCtrlTx;
