library verilog;
use verilog.vl_types.all;
entity StreamFifo_8 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload : in     vl_logic_vector(7 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload  : out    vl_logic_vector(7 downto 0);
        io_flush        : in     vl_logic;
        io_occupancy    : out    vl_logic_vector(7 downto 0);
        io_availability : out    vl_logic_vector(7 downto 0);
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end StreamFifo_8;
