library verilog;
use verilog.vl_types.all;
entity BufferCC_20 is
    port(
        io_dataIn       : in     vl_logic;
        io_dataOut      : out    vl_logic;
        io_memoryClk    : in     vl_logic;
        io_ddrMasters_0_reset: in     vl_logic
    );
end BufferCC_20;
