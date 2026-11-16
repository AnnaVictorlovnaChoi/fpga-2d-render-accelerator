library verilog;
use verilog.vl_types.all;
entity GPU_CACHE is
    port(
        re              : in     vl_logic;
        we              : in     vl_logic;
        addren          : in     vl_logic;
        reset           : in     vl_logic;
        addr            : in     vl_logic_vector(7 downto 0);
        wdata_a         : in     vl_logic_vector(63 downto 0);
        rdata_a         : out    vl_logic_vector(63 downto 0);
        clk             : in     vl_logic
    );
end GPU_CACHE;
