library verilog;
use verilog.vl_types.all;
entity BufferCC_4 is
    port(
        io_dataIn       : in     vl_logic_vector(6 downto 0);
        io_dataOut      : out    vl_logic_vector(6 downto 0);
        io_memoryClk    : in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end BufferCC_4;
