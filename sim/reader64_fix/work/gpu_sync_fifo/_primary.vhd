library verilog;
use verilog.vl_types.all;
entity gpu_sync_fifo is
    generic(
        WIDTH           : integer := 32;
        DEPTH           : integer := 256;
        PROG_FULL_LEVEL : vl_logic_vector(31 downto 0);
        WR_WIDTH        : vl_logic_vector(31 downto 0);
        RD_WIDTH        : vl_logic_vector(31 downto 0)
    );
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        wr_en           : in     vl_logic;
        wr_data         : in     vl_logic_vector;
        wr_keep         : in     vl_logic_vector;
        rd_en           : in     vl_logic;
        rd_data         : out    vl_logic_vector;
        full            : out    vl_logic;
        prog_full       : out    vl_logic;
        empty           : out    vl_logic;
        count_used      : out    vl_logic_vector(7 downto 0)
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of WIDTH : constant is 2;
    attribute mti_svvh_generic_type of DEPTH : constant is 2;
    attribute mti_svvh_generic_type of PROG_FULL_LEVEL : constant is 4;
    attribute mti_svvh_generic_type of WR_WIDTH : constant is 4;
    attribute mti_svvh_generic_type of RD_WIDTH : constant is 4;
end gpu_sync_fifo;
