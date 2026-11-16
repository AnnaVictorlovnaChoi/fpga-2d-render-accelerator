library verilog;
use verilog.vl_types.all;
entity StreamFifoCC_10 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_data: in     vl_logic_vector(63 downto 0);
        io_push_payload_strb: in     vl_logic_vector(7 downto 0);
        io_push_payload_last: in     vl_logic;
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_data: out    vl_logic_vector(63 downto 0);
        io_pop_payload_strb: out    vl_logic_vector(7 downto 0);
        io_pop_payload_last: out    vl_logic;
        io_pushOccupancy: out    vl_logic_vector(4 downto 0);
        io_popOccupancy : out    vl_logic_vector(4 downto 0);
        io_ddrMasters_0_clk: in     vl_logic;
        io_ddrMasters_0_reset: in     vl_logic;
        io_memoryClk    : in     vl_logic;
        system_ddr_ddrLogic_userAdapters_1_bridge_toplevel_io_ddrMasters_0_reset_synchronized_1: in     vl_logic
    );
end StreamFifoCC_10;
