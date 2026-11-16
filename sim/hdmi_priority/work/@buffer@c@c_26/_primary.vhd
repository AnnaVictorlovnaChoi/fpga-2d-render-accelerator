library verilog;
use verilog.vl_types.all;
entity BufferCC_26 is
    port(
        io_dataIn       : in     vl_logic_vector(4 downto 0);
        io_dataOut      : out    vl_logic_vector(4 downto 0);
        io_memoryClk    : in     vl_logic;
        system_ddr_ddrLogic_userAdapters_1_bridge_toplevel_io_ddrMasters_0_reset_synchronized_1: in     vl_logic
    );
end BufferCC_26;
