library verilog;
use verilog.vl_types.all;
entity StreamFifo_3 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_startAt: in     vl_logic_vector(3 downto 0);
        io_push_payload_endAt: in     vl_logic_vector(3 downto 0);
        io_push_payload_size: in     vl_logic_vector(2 downto 0);
        io_push_payload_id: in     vl_logic_vector(3 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_startAt: out    vl_logic_vector(3 downto 0);
        io_pop_payload_endAt: out    vl_logic_vector(3 downto 0);
        io_pop_payload_size: out    vl_logic_vector(2 downto 0);
        io_pop_payload_id: out    vl_logic_vector(3 downto 0);
        io_flush        : in     vl_logic;
        io_occupancy    : out    vl_logic_vector(2 downto 0);
        io_availability : out    vl_logic_vector(2 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end StreamFifo_3;
