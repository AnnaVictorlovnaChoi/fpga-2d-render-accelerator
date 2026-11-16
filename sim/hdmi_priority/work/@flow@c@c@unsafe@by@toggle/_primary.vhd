library verilog;
use verilog.vl_types.all;
entity FlowCCUnsafeByToggle is
    port(
        io_input_valid  : in     vl_logic;
        io_input_payload_last: in     vl_logic;
        io_input_payload_fragment: in     vl_logic_vector(0 downto 0);
        io_output_valid : out    vl_logic;
        io_output_payload_last: out    vl_logic;
        io_output_payload_fragment: out    vl_logic_vector(0 downto 0);
        jtagCtrl_tck    : in     vl_logic;
        io_systemClk    : in     vl_logic;
        debugCd_logic_outputReset: in     vl_logic
    );
end FlowCCUnsafeByToggle;
