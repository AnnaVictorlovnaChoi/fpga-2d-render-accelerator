library verilog;
use verilog.vl_types.all;
entity BufferCC_1 is
    port(
        io_dataIn       : in     vl_logic_vector(6 downto 0);
        io_dataOut      : out    vl_logic_vector(6 downto 0);
        io_systemClk    : in     vl_logic;
        systemCd_logic_outputReset: in     vl_logic
    );
end BufferCC_1;
