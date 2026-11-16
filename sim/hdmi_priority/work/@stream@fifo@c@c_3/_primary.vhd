library verilog;
use verilog.vl_types.all;
entity StreamFifoCC_3 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_data: in     vl_logic_vector(63 downto 0);
        io_push_payload_id: in     vl_logic_vector(3 downto 0);
        io_push_payload_resp: in     vl_logic_vector(1 downto 0);
        io_push_payload_last: in     vl_logic;
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_data: out    vl_logic_vector(63 downto 0);
        io_pop_payload_id: out    vl_logic_vector(3 downto 0);
        io_pop_payload_resp: out    vl_logic_vector(1 downto 0);
        io_pop_payload_last: out    vl_logic;
        io_pushOccupancy: out    vl_logic_vector(4 downto 0);
        io_popOccupancy : out    vl_logic_vector(4 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic;
        io_ddrMasters_1_clk: in     vl_logic;
        system_ddr_ddrLogic_userAdapters_0_bridge_toplevel_ddrCd_logic_outputReset_synchronized_1: out    vl_logic
    );
end StreamFifoCC_3;
