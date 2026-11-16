library verilog;
use verilog.vl_types.all;
entity BufferCC_7 is
    port(
        io_dataIn       : in     vl_logic_vector(4 downto 0);
        io_dataOut      : out    vl_logic_vector(4 downto 0);
        io_ddrMasters_1_clk: in     vl_logic;
        io_ddrMasters_1_reset: in     vl_logic
    );
end BufferCC_7;
