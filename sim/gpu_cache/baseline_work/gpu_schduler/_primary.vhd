library verilog;
use verilog.vl_types.all;
entity gpu_schduler is
    generic(
        WIDTH           : integer := 32;
        DEPTH           : integer := 256;
        PROG_FULL_LEVEL : vl_logic_vector(31 downto 0)
    );
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        cmd_fifo_empty  : in     vl_logic;
        cmd_fifo_rd_en  : out    vl_logic;
        cmd_fifo_rd_data: in     vl_logic_vector(31 downto 0);
        cmd_fifo_word_count: in     vl_logic_vector(7 downto 0);
        ctrl            : out    vl_logic_vector(31 downto 0);
        src             : out    vl_logic_vector(31 downto 0);
        dst             : out    vl_logic_vector(31 downto 0);
        wh              : out    vl_logic_vector(31 downto 0);
        src_stride      : out    vl_logic_vector(31 downto 0);
        dst_stride      : out    vl_logic_vector(31 downto 0);
        color           : out    vl_logic_vector(31 downto 0);
        \select\        : out    vl_logic;
        parser0_ready   : in     vl_logic;
        parser0_busy    : in     vl_logic;
        parser1_ready   : in     vl_logic;
        parser1_busy    : in     vl_logic;
        parser0_cmd_valid: out    vl_logic;
        parser1_cmd_valid: out    vl_logic;
        scheduler_busy  : out    vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of WIDTH : constant is 2;
    attribute mti_svvh_generic_type of DEPTH : constant is 2;
    attribute mti_svvh_generic_type of PROG_FULL_LEVEL : constant is 4;
end gpu_schduler;
