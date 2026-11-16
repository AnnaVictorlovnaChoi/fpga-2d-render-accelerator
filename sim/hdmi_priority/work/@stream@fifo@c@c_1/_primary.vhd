library verilog;
use verilog.vl_types.all;
entity StreamFifoCC_1 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_last: in     vl_logic;
        io_push_payload_fragment_source: in     vl_logic_vector(0 downto 0);
        io_push_payload_fragment_opcode: in     vl_logic_vector(0 downto 0);
        io_push_payload_fragment_data: in     vl_logic_vector(127 downto 0);
        io_push_payload_fragment_context: in     vl_logic_vector(4 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_last: out    vl_logic;
        io_pop_payload_fragment_source: out    vl_logic_vector(0 downto 0);
        io_pop_payload_fragment_opcode: out    vl_logic_vector(0 downto 0);
        io_pop_payload_fragment_data: out    vl_logic_vector(127 downto 0);
        io_pop_payload_fragment_context: out    vl_logic_vector(4 downto 0);
        io_pushOccupancy: out    vl_logic_vector(6 downto 0);
        io_popOccupancy : out    vl_logic_vector(6 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic;
        io_systemClk    : in     vl_logic
    );
end StreamFifoCC_1;
