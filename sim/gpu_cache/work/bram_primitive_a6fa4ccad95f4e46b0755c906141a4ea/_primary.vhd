library verilog;
use verilog.vl_types.all;
entity bram_primitive_a6fa4ccad95f4e46b0755c906141a4ea is
    generic(
        FAMILY          : string  := "TITANIUM";
        WRITE_WIDTH     : integer := 16;
        WCLK_POLARITY   : vl_logic := Hi1;
        WCLKE_POLARITY  : vl_logic := Hi1;
        WE_POLARITY     : vl_logic_vector(0 downto 0) := (others => Hi1);
        WRITE_MODE      : string  := "READ_FIRST";
        READ_WIDTH      : integer := 16;
        RCLK_POLARITY   : vl_logic := Hi1;
        RE_POLARITY     : vl_logic := Hi1;
        OUTPUT_REG      : vl_logic := Hi0;
        RST_POLARITY    : vl_logic := Hi1;
        RESET_RAM       : string  := "ASYNC";
        RESET_OUTREG    : string  := "ASYNC";
        WADDREN_POLARITY: vl_logic := Hi1;
        RADDREN_POLARITY: vl_logic := Hi1;
        WEN_WIDTH       : integer := 1;
        ini_index       : integer := 0
    );
    port(
        WCLK            : in     vl_logic;
        WCLKE           : in     vl_logic;
        WE              : in     vl_logic_vector;
        WADDR           : in     vl_logic_vector;
        WDATA           : in     vl_logic_vector;
        RCLK            : in     vl_logic;
        RE              : in     vl_logic;
        RADDR           : in     vl_logic_vector;
        RDATA           : out    vl_logic_vector;
        RST             : in     vl_logic;
        WADDREN         : in     vl_logic;
        RADDREN         : in     vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of FAMILY : constant is 1;
    attribute mti_svvh_generic_type of WRITE_WIDTH : constant is 1;
    attribute mti_svvh_generic_type of WCLK_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of WCLKE_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of WE_POLARITY : constant is 2;
    attribute mti_svvh_generic_type of WRITE_MODE : constant is 1;
    attribute mti_svvh_generic_type of READ_WIDTH : constant is 1;
    attribute mti_svvh_generic_type of RCLK_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of RE_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of OUTPUT_REG : constant is 1;
    attribute mti_svvh_generic_type of RST_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of RESET_RAM : constant is 1;
    attribute mti_svvh_generic_type of RESET_OUTREG : constant is 1;
    attribute mti_svvh_generic_type of WADDREN_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of RADDREN_POLARITY : constant is 1;
    attribute mti_svvh_generic_type of WEN_WIDTH : constant is 1;
    attribute mti_svvh_generic_type of ini_index : constant is 1;
end bram_primitive_a6fa4ccad95f4e46b0755c906141a4ea;
