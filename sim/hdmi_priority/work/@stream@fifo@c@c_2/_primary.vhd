library verilog;
use verilog.vl_types.all;
entity StreamFifoCC_2 is
    port(
        io_push_valid   : in     vl_logic;
        io_push_ready   : out    vl_logic;
        io_push_payload_addr: in     vl_logic_vector(31 downto 0);
        io_push_payload_id: in     vl_logic_vector(3 downto 0);
        io_push_payload_region: in     vl_logic_vector(3 downto 0);
        io_push_payload_len: in     vl_logic_vector(7 downto 0);
        io_push_payload_size: in     vl_logic_vector(2 downto 0);
        io_push_payload_burst: in     vl_logic_vector(1 downto 0);
        io_push_payload_lock: in     vl_logic_vector(0 downto 0);
        io_push_payload_cache: in     vl_logic_vector(3 downto 0);
        io_push_payload_qos: in     vl_logic_vector(3 downto 0);
        io_push_payload_prot: in     vl_logic_vector(2 downto 0);
        io_pop_valid    : out    vl_logic;
        io_pop_ready    : in     vl_logic;
        io_pop_payload_addr: out    vl_logic_vector(31 downto 0);
        io_pop_payload_id: out    vl_logic_vector(3 downto 0);
        io_pop_payload_region: out    vl_logic_vector(3 downto 0);
        io_pop_payload_len: out    vl_logic_vector(7 downto 0);
        io_pop_payload_size: out    vl_logic_vector(2 downto 0);
        io_pop_payload_burst: out    vl_logic_vector(1 downto 0);
        io_pop_payload_lock: out    vl_logic_vector(0 downto 0);
        io_pop_payload_cache: out    vl_logic_vector(3 downto 0);
        io_pop_payload_qos: out    vl_logic_vector(3 downto 0);
        io_pop_payload_prot: out    vl_logic_vector(2 downto 0);
        io_pushOccupancy: out    vl_logic_vector(4 downto 0);
        io_popOccupancy : out    vl_logic_vector(4 downto 0);
        io_ddrMasters_1_clk: in     vl_logic;
        io_ddrMasters_1_reset: in     vl_logic;
        io_memoryClk    : in     vl_logic;
        system_ddr_ddrLogic_userAdapters_0_bridge_toplevel_io_ddrMasters_1_reset_synchronized_1: out    vl_logic
    );
end StreamFifoCC_2;
