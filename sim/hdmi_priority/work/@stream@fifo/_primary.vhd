library verilog;
use verilog.vl_types.all;
entity StreamFifo is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_source: in     vl_logic_vector(0 downto 0);
        io_push_payload_context: in     vl_logic_vector(4 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_source: out    vl_logic_vector(0 downto 0);
        io_pop_payload_context: out    vl_logic_vector(4 downto 0);
        io_flush        : in     vl_logic;
        io_occupancy    : out    vl_logic_vector(6 downto 0);
        io_availability : out    vl_logic_vector(6 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end StreamFifo;
