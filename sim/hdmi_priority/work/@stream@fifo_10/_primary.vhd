library verilog;
use verilog.vl_types.all;
entity StreamFifo_10 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_kind: in     vl_logic;
        io_push_payload_read: in     vl_logic;
        io_push_payload_write: in     vl_logic;
        io_push_payload_data: in     vl_logic_vector(7 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_kind: out    vl_logic;
        io_pop_payload_read: out    vl_logic;
        io_pop_payload_write: out    vl_logic;
        io_pop_payload_data: out    vl_logic_vector(7 downto 0);
        io_flush        : in     vl_logic;
        io_occupancy    : out    vl_logic_vector(8 downto 0);
        io_availability : out    vl_logic_vector(8 downto 0);
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end StreamFifo_10;
