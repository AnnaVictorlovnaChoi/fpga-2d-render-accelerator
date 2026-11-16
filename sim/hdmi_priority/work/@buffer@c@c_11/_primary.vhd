library verilog;
use verilog.vl_types.all;
entity BufferCC_11 is
    port(
        io_dataIn       : in     vl_logic;
        io_dataOut      : out    vl_logic;
        io_ddrMasters_1_clk: in     vl_logic;
        ddrCd_logic_outputReset: in     vl_logic
    );
end BufferCC_11;
