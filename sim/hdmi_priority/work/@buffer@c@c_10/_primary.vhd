library verilog;
use verilog.vl_types.all;
entity BufferCC_10 is
    port(
        io_dataIn       : in     vl_logic_vector(4 downto 0);
        io_dataOut      : out    vl_logic_vector(4 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end BufferCC_10;
