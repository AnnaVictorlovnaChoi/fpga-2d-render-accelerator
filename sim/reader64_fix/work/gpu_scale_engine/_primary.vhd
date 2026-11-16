library verilog;
use verilog.vl_types.all;
entity gpu_scale_engine is
    generic(
        ADDR_WIDTH      : integer := 32
    );
    port(
        src_base        : in     vl_logic_vector;
        src_stride_bytes: in     vl_logic_vector(31 downto 0);
        dst_x           : in     vl_logic_vector(15 downto 0);
        dst_y           : in     vl_logic_vector(15 downto 0);
        scale_q26       : in     vl_logic_vector(7 downto 0);
        valid           : out    vl_logic;
        src_x           : out    vl_logic_vector(31 downto 0);
        src_y           : out    vl_logic_vector(31 downto 0);
        src_addr        : out    vl_logic_vector
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of ADDR_WIDTH : constant is 2;
end gpu_scale_engine;
