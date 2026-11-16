library verilog;
use verilog.vl_types.all;
entity BufferCC is
    port(
        io_dataIn       : in     vl_logic;
        io_dataOut      : out    vl_logic;
        io_systemClk    : in     vl_logic;
        debugCd_logic_outputReset: in     vl_logic
    );
end BufferCC;
