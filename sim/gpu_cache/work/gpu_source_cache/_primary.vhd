library verilog;
use verilog.vl_types.all;
entity gpu_source_cache is
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        start           : in     vl_logic;
        ready           : out    vl_logic;
        operation       : in     vl_logic_vector(2 downto 0);
        control         : in     vl_logic_vector(3 downto 0);
        length_bytes    : in     vl_logic_vector(31 downto 0);
        rows            : in     vl_logic_vector(6 downto 0);
        first_row       : in     vl_logic_vector(15 downto 0);
        last_batch      : in     vl_logic;
        busy            : out    vl_logic;
        done            : out    vl_logic;
        error           : out    vl_logic;
        ddr_start       : out    vl_logic;
        replay_active   : out    vl_logic;
        ddr_done        : in     vl_logic;
        ddr_error       : in     vl_logic;
        in_valid        : in     vl_logic;
        in_data         : in     vl_logic_vector(63 downto 0);
        in_keep         : in     vl_logic_vector(3 downto 0);
        in_last         : in     vl_logic;
        in_full         : out    vl_logic;
        fifo_full       : in     vl_logic;
        out_valid       : out    vl_logic;
        out_data        : out    vl_logic_vector(63 downto 0);
        out_keep        : out    vl_logic_vector(3 downto 0);
        out_last        : out    vl_logic
    );
end gpu_source_cache;
