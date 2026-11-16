library verilog;
use verilog.vl_types.all;
entity BufferCC_18 is
    port(
        io_dataIn       : in     vl_logic_vector(4 downto 0);
        io_dataOut      : out    vl_logic_vector(4 downto 0);
        io_ddrMasters_1_clk: in     vl_logic;
        system_ddr_ddrLogic_userAdapters_0_bridge_toplevel_ddrCd_logic_outputReset_synchronized_1: in     vl_logic
    );
end BufferCC_18;
