library verilog;
use verilog.vl_types.all;
entity gpu_scale_x_sampler is
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        enable          : in     vl_logic;
        line_start      : in     vl_logic;
        scale_q26       : in     vl_logic_vector(7 downto 0);
        dst_width       : in     vl_logic_vector(15 downto 0);
        src_fifo_empty  : in     vl_logic;
        src_fifo_data   : in     vl_logic_vector(15 downto 0);
        src_fifo_rd_en  : out    vl_logic;
        out_valid       : out    vl_logic;
        out_ready       : in     vl_logic;
        out_data        : out    vl_logic_vector(15 downto 0);
        line_done       : out    vl_logic
    );
end gpu_scale_x_sampler;
