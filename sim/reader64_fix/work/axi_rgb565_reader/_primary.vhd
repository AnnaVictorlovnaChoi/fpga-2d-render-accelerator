library verilog;
use verilog.vl_types.all;
entity axi_rgb565_reader is
    generic(
        AXI_ADDR_WIDTH  : integer := 32;
        AXI_ID_WIDTH    : integer := 4;
        BURST_LEN       : integer := 8;
        DESC_DEPTH      : integer := 32
    );
    port(
        clk             : in     vl_logic;
        rst_n           : in     vl_logic;
        cmd_valid       : in     vl_logic;
        cmd_ready       : out    vl_logic;
        cmd_addr        : in     vl_logic_vector;
        cmd_length_bytes: in     vl_logic_vector(31 downto 0);
        cmd_stride_bytes: in     vl_logic_vector(31 downto 0);
        cmd_rows        : in     vl_logic_vector(6 downto 0);
        busy            : out    vl_logic;
        done            : out    vl_logic;
        error           : out    vl_logic;
        bus_request     : out    vl_logic;
        bus_yield       : out    vl_logic;
        pause_new_bursts: in     vl_logic;
        fifo_full       : in     vl_logic;
        fifo_wr_en      : out    vl_logic;
        fifo_wr_data    : out    vl_logic_vector(63 downto 0);
        fifo_wr_keep    : out    vl_logic_vector(3 downto 0);
        fifo_wr_last    : out    vl_logic;
        m_axi_arid      : out    vl_logic_vector;
        m_axi_araddr    : out    vl_logic_vector;
        m_axi_arlen     : out    vl_logic_vector(7 downto 0);
        m_axi_arsize    : out    vl_logic_vector(2 downto 0);
        m_axi_arburst   : out    vl_logic_vector(1 downto 0);
        m_axi_arcache   : out    vl_logic_vector(3 downto 0);
        m_axi_arqos     : out    vl_logic_vector(3 downto 0);
        m_axi_arregion  : out    vl_logic_vector(3 downto 0);
        m_axi_arprot    : out    vl_logic_vector(2 downto 0);
        m_axi_arlock    : out    vl_logic;
        m_axi_arvalid   : out    vl_logic;
        m_axi_arready   : in     vl_logic;
        m_axi_rid       : in     vl_logic_vector;
        m_axi_rdata     : in     vl_logic_vector(63 downto 0);
        m_axi_rresp     : in     vl_logic_vector(1 downto 0);
        m_axi_rlast     : in     vl_logic;
        m_axi_rvalid    : in     vl_logic;
        m_axi_rready    : out    vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of AXI_ADDR_WIDTH : constant is 2;
    attribute mti_svvh_generic_type of AXI_ID_WIDTH : constant is 2;
    attribute mti_svvh_generic_type of BURST_LEN : constant is 2;
    attribute mti_svvh_generic_type of DESC_DEPTH : constant is 2;
end axi_rgb565_reader;
