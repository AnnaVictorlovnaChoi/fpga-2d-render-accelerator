library verilog;
use verilog.vl_types.all;
entity BufferCC_19 is
    port(
        io_dataIn       : in     vl_logic_vector(4 downto 0);
        io_dataOut      : out    vl_logic_vector(4 downto 0);
        io_ddrMasters_0_clk: in     vl_logic;
        io_ddrMasters_0_reset: in     vl_logic
    );
end BufferCC_19;
