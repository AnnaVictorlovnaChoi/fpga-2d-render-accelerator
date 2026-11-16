library verilog;
use verilog.vl_types.all;
entity bitblt_cmd_parser8 is
    generic(
        CORE_COUNT      : integer := 8;
        ADDR_WIDTH      : integer := 32
    );
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        ctrl            : in     vl_logic_vector(31 downto 0);
        src             : in     vl_logic_vector(31 downto 0);
        dst             : in     vl_logic_vector(31 downto 0);
        wh              : in     vl_logic_vector(31 downto 0);
        src_stride      : in     vl_logic_vector(31 downto 0);
        dst_stride      : in     vl_logic_vector(31 downto 0);
        color           : in     vl_logic_vector(31 downto 0);
        cmd_valid       : in     vl_logic_vector;
        op              : out    vl_logic_vector;
        color_out       : out    vl_logic_vector;
        cache_control   : out    vl_logic_vector;
        cache_row       : out    vl_logic_vector;
        cache_last_batch: out    vl_logic_vector;
        scale_q26       : out    vl_logic_vector;
        scale_src_base  : out    vl_logic_vector;
        scale_src_stride: out    vl_logic_vector;
        read_cmd_valid  : out    vl_logic_vector;
        read_cmd_ready  : in     vl_logic_vector;
        read_cmd_addr   : out    vl_logic_vector;
        read_cmd_length_bytes: out    vl_logic_vector;
        read_cmd_stride_bytes: out    vl_logic_vector;
        read_cmd_rows   : out    vl_logic_vector;
        read_busy       : in     vl_logic_vector;
        read_done       : in     vl_logic_vector;
        read_error      : in     vl_logic_vector;
        dst_read_cmd_valid: out    vl_logic_vector;
        dst_read_cmd_ready: in     vl_logic_vector;
        dst_read_cmd_addr: out    vl_logic_vector;
        dst_read_cmd_length_bytes: out    vl_logic_vector;
        dst_read_cmd_stride_bytes: out    vl_logic_vector;
        dst_read_cmd_rows: out    vl_logic_vector;
        dst_read_busy   : in     vl_logic_vector;
        dst_read_done   : in     vl_logic_vector;
        dst_read_error  : in     vl_logic_vector;
        write_cmd_valid : out    vl_logic_vector;
        write_cmd_ready : in     vl_logic_vector;
        write_cmd_addr  : out    vl_logic_vector;
        write_cmd_length_bytes: out    vl_logic_vector;
        write_busy      : in     vl_logic_vector;
        write_done      : in     vl_logic_vector;
        write_error     : in     vl_logic_vector;
        bitblt_busy     : out    vl_logic_vector;
        bitblt_done     : out    vl_logic_vector;
        bitblt_error    : out    vl_logic_vector
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of CORE_COUNT : constant is 2;
    attribute mti_svvh_generic_type of ADDR_WIDTH : constant is 2;
end bitblt_cmd_parser8;
