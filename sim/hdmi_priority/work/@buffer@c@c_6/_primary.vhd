library verilog;
use verilog.vl_types.all;
entity BufferCC_6 is
    port(
        io_dataIn       : in     vl_logic_vector(6 downto 0);
        io_dataOut      : out    vl_logic_vector(6 downto 0);
        io_systemClk    : in     vl_logic;
        system_ddr_ddrLogic_cc_fifo_toplevel_ddrCd_logic_outputReset_synchronized: in     vl_logic
    );
end BufferCC_6;
