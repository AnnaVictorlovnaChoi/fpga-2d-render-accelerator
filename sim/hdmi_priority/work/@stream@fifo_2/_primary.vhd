library verilog;
use verilog.vl_types.all;
entity StreamFifo_2 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload : in     vl_logic_vector(1 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload  : out    vl_logic_vector(1 downto 0);
        io_flush        : in     vl_logic;
        io_occupancy    : out    vl_logic_vector(2 downto 0);
        io_availability : out    vl_logic_vector(2 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end StreamFifo_2;
