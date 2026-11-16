library verilog;
use verilog.vl_types.all;
entity gpu_schduler8 is
    generic(
        CORE_COUNT      : integer := 8
    );
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        cmd_fifo_empty  : in     vl_logic;
        cmd_fifo_word_count: in     vl_logic_vector(7 downto 0);
        cmd_fifo_rd_en  : out    vl_logic;
        cmd_fifo_rd_data: in     vl_logic_vector(31 downto 0);
        ctrl            : out    vl_logic_vector(31 downto 0);
        src             : out    vl_logic_vector(31 downto 0);
        dst             : out    vl_logic_vector(31 downto 0);
        wh              : out    vl_logic_vector(31 downto 0);
        src_stride      : out    vl_logic_vector(31 downto 0);
        dst_stride      : out    vl_logic_vector(31 downto 0);
        color           : out    vl_logic_vector(31 downto 0);
        parser_ready    : in     vl_logic_vector;
        parser_busy     : in     vl_logic_vector;
        cache_access_done: in     vl_logic_vector;
        parser_cmd_valid: out    vl_logic_vector;
        scheduler_busy  : out    vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of CORE_COUNT : constant is 2;
end gpu_schduler8;
