library verilog;
use verilog.vl_types.all;
entity fifo_check is
    generic(
        RATIO           : integer := 4
    );
    port(
        finished        : out    vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of RATIO : constant is 1;
end fifo_check;
