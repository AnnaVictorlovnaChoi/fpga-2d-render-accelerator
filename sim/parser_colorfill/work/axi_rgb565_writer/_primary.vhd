library verilog;
use verilog.vl_types.all;
entity axi_rgb565_writer is
    generic(
        AXI_ADDR_WIDTH  : integer := 32;
        AXI_ID_WIDTH    : integer := 4;
        BURST_LEN       : integer := 16
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
        bus_request     : out    vl_logic;
        bus_yield       : out    vl_logic;
        bus_grant       : in     vl_logic;
        burst_done      : out    vl_logic;
        busy            : out    vl_logic;
        done            : out    vl_logic;
        error           : out    vl_logic;
        fifo_valid      : in     vl_logic;
        fifo_ready      : out    vl_logic;
        fifo_data       : in     vl_logic_vector(15 downto 0);
        m_axi_awid      : out    vl_logic_vector;
        m_axi_awaddr    : out    vl_logic_vector;
        m_axi_awlen     : out    vl_logic_vector(7 downto 0);
        m_axi_awsize    : out    vl_logic_vector(2 downto 0);
        m_axi_awburst   : out    vl_logic_vector(1 downto 0);
        m_axi_awcache   : out    vl_logic_vector(3 downto 0);
        m_axi_awqos     : out    vl_logic_vector(3 downto 0);
        m_axi_awregion  : out    vl_logic_vector(3 downto 0);
        m_axi_awprot    : out    vl_logic_vector(2 downto 0);
        m_axi_awlock    : out    vl_logic;
        m_axi_awvalid   : out    vl_logic;
        m_axi_awready   : in     vl_logic;
        m_axi_wdata     : out    vl_logic_vector(31 downto 0);
        m_axi_wstrb     : out    vl_logic_vector(3 downto 0);
        m_axi_wlast     : out    vl_logic;
        m_axi_wvalid    : out    vl_logic;
        m_axi_wready    : in     vl_logic;
        m_axi_bid       : in     vl_logic_vector;
        m_axi_bresp     : in     vl_logic_vector(1 downto 0);
        m_axi_bvalid    : in     vl_logic;
        m_axi_bready    : out    vl_logic
    );
    attribute mti_svvh_generic_type : integer;
    attribute mti_svvh_generic_type of AXI_ADDR_WIDTH : constant is 2;
    attribute mti_svvh_generic_type of AXI_ID_WIDTH : constant is 2;
    attribute mti_svvh_generic_type of BURST_LEN : constant is 2;
end axi_rgb565_writer;
