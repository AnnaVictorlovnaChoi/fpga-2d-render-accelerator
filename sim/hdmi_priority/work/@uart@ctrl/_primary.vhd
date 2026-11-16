library verilog;
use verilog.vl_types.all;
entity UartCtrl is
    port(
        io_config_frame_dataLength: in     vl_logic_vector(2 downto 0);
        io_config_frame_stop: in     vl_logic_vector(0 downto 0);
        io_config_frame_parity: in     vl_logic_vector(1 downto 0);
        io_config_clockDivider: in     vl_logic_vector(19 downto 0);
        io_write_valid  : in     vl_logic;
        io_write_ready  : out    vl_logic;
        io_write_payload: in     vl_logic_vector(7 downto 0);
        io_read_valid   : out    vl_logic;
        io_read_ready   : in     vl_logic;
        io_read_payload : out    vl_logic_vector(7 downto 0);
        io_uart_txd     : out    vl_logic;
        io_uart_rxd     : in     vl_logic;
        io_readError    : out    vl_logic;
        io_writeBreak   : in     vl_logic;
        io_readBreak    : out    vl_logic;
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end UartCtrl;
