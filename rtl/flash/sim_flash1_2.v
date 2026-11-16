`default_nettype none

// 一个逻辑 16 MiB SPI Flash 到两颗 8 MiB Flash 的端口映射。
// 逻辑地址 0x000000-0x7FFFFF 选择 Flash1，
// 逻辑地址 0x800000-0xFFFFFF 选择 Flash2。
module sim_flash1_2 #(
    // 0: Flash1 transparent bypass; 1: map two 8 MiB devices into 16 MiB.
    parameter integer FLASH2_PRESENT = 1
)
(
    // Stable fabric clock/reset used by the protocol parser.  Never clock
    // fabric registers directly from the routed SPI SCLK signal.
    input  wire logic_clk,
    input  wire logic_reset_n,

    // 逻辑 SPI 端口
    input  wire system_spi_0_io_sclk_write,
    input  wire system_spi_0_io_data_0_OE,
    output wire system_spi_0_io_data_0_IN,
    input  wire system_spi_0_io_data_0_OUT,
    input  wire system_spi_0_io_data_1_OE,
    output wire system_spi_0_io_data_1_IN,
    input  wire system_spi_0_io_data_1_OUT,
    input  wire system_spi_0_cs_n,

    // Flash1 物理端口
    output wire flash1_sclk,
    output wire flash1_cs_n,
    output wire flash1_io0_OE,
    output wire flash1_io0_OUT,
    input  wire flash1_io0_IN,
    output wire flash1_io1_OE,
    output wire flash1_io1_OUT,
    input  wire flash1_io1_IN,

    // Flash2 物理端口
    output wire flash2_sclk,
    output wire flash2_cs_n,
    output wire flash2_io0_OE,
    output wire flash2_io0_OUT,
    input  wire flash2_io0_IN,
    output wire flash2_io1_OE,
    output wire flash2_io1_OUT,
    input  wire flash2_io1_IN
);

generate
if (!FLASH2_PRESENT) begin : g_flash1_only
    // Flash2 未安装时，桥接器必须是严格透明的。尤其不能解析 opcode
    // 或在命令阶段拉低第二路 CS，否则会改变 Flash1 的 SPI 时序。
    assign flash1_sclk = system_spi_0_io_sclk_write;
    assign flash1_cs_n = system_spi_0_cs_n;
    assign flash1_io0_OE = system_spi_0_io_data_0_OE;
    assign flash1_io0_OUT = system_spi_0_io_data_0_OUT;
    assign flash1_io1_OE = system_spi_0_io_data_1_OE;
    assign flash1_io1_OUT = system_spi_0_io_data_1_OUT;
    assign system_spi_0_io_data_0_IN = flash1_io0_IN;
    assign system_spi_0_io_data_1_IN = flash1_io1_IN;

    assign flash2_sclk = 1'b0;
    assign flash2_cs_n = 1'b1;
    assign flash2_io0_OE = 1'b0;
    assign flash2_io0_OUT = 1'b0;
    assign flash2_io1_OE = 1'b0;
    assign flash2_io1_OUT = 1'b0;
end else begin : g_flash1_flash2
    localparam [7:0] CMD_PAGE_PROGRAM = 8'h02;
    localparam [7:0] CMD_READ_DATA = 8'h03;
    localparam [7:0] CMD_FAST_READ = 8'h0B;
    localparam [7:0] CMD_DUAL_READ = 8'h3B;
    localparam [7:0] CMD_DUAL_IO_READ = 8'hBB;
    localparam [7:0] CMD_QUAD_READ = 8'h6B;
    localparam [7:0] CMD_QUAD_IO_READ = 8'hEB;
    localparam [7:0] CMD_QUAD_PROGRAM = 8'h32;
    localparam [7:0] CMD_QUAD_PROGRAM_2 = 8'h38;
    localparam [7:0] CMD_SECTOR_ERASE = 8'h20;
    localparam [7:0] CMD_BLOCK_ERASE_32K = 8'h52;
    localparam [7:0] CMD_BLOCK_ERASE_64K = 8'hD8;

    reg [7:0] command_shift = 8'h00;
    reg [7:0] command = 8'h00;
    reg [4:0] bit_count = 5'd0;
    reg       active_flash = 1'b0;
    reg       route_flash = 1'b0;
    reg       route_valid = 1'b0;
    reg       broadcast = 1'b0;
    reg       sclk_d = 1'b0;

    function address_command;
        input [7:0] value;
        begin
            case (value)
                CMD_PAGE_PROGRAM, CMD_READ_DATA, CMD_FAST_READ,
                CMD_DUAL_READ,
                CMD_SECTOR_ERASE, CMD_BLOCK_ERASE_32K,
                CMD_BLOCK_ERASE_64K: address_command = 1'b1;
                default: address_command = 1'b0;
            endcase
        end
    endfunction

    // Commands that must update both physical devices.  In particular, every
    // erase/program sequence starts with WREN; routing that command to only
    // the previously selected bank leaves the other chip's WEL bit cleared.
    function broadcast_command;
        input [7:0] value;
        begin
            case (value)
                8'h04, // Write Disable
                8'h06, // Write Enable
                8'h50, // Volatile SR Write Enable
                8'h66, // Reset Enable
                8'h99, // Reset
                8'h98, // Global block/unlock (vendor dependent)
                8'h60, // Chip Erase
                8'hC7: broadcast_command = 1'b1;
                default: broadcast_command = 1'b0;
            endcase
        end
    endfunction

    wire transaction_active = !system_spi_0_cs_n;
    wire command_known = (bit_count >= 5'd8);
    wire sclk_rise = system_spi_0_io_sclk_write && !sclk_d;

    // 第一个地址位作为虚拟 bank 位，不发送给实际 Flash。
    // 本桥只支持单线命令/地址，以及 0x3B 的双线数据阶段；顶层没有
    // SPI IO2/IO3，因此不能把 Quad 命令当成可支持命令。
    // route_flash 只能使用已经在 SCLK 上升沿锁存的地址位。原先在
    // bit_count==8 时直接使用组合 MOSI，会把命令的最后一位短暂当成
    // bank 位，从而在命令和地址之间产生 CS_n 毛刺。
    wire current_flash = route_flash;
    wire address_active = address_command(command);
    // IO2/IO3 are not present in this wrapper/top-level. Abort quad commands
    // and dual-I/O address commands after their opcode instead of accidentally
    // sending a partial operation.  0x3B remains supported because its command
    // and address are single-lane; only its returned data is dual-lane.
    wire unsupported_multi_io_command =
        (command == CMD_DUAL_IO_READ) ||
        (command == CMD_QUAD_READ) ||
        (command == CMD_QUAD_IO_READ) ||
        (command == CMD_QUAD_PROGRAM) ||
        (command == CMD_QUAD_PROGRAM_2);
    wire physical_data0 =
        (address_active && bit_count == 5'd8) ? 1'b0 :
        system_spi_0_io_data_0_OUT;

    // Parse the serial stream in the same 100 MHz core-clock domain that
    // generates the SoC SPI pins and clocks the Interface Designer GPIO
    // registers.  The previous implementation used SCLK as a fabric clock;
    // that created an unconstrained/high-skew generated-clock path whose
    // behavior changed when the Flash2 pins moved to another IO bank.
    always @(posedge logic_clk or negedge logic_reset_n) begin
        if (!logic_reset_n) begin
            command_shift <= 8'h00;
            command <= 8'h00;
            bit_count <= 5'd0;
            active_flash <= 1'b0;
            route_flash <= 1'b0;
            route_valid <= 1'b0;
            broadcast <= 1'b0;
            sclk_d <= 1'b0;
        end else begin
            sclk_d <= system_spi_0_io_sclk_write;

            if (system_spi_0_cs_n) begin
                command_shift <= 8'h00;
                command <= 8'h00;
                bit_count <= 5'd0;
                route_flash <= active_flash;
                route_valid <= 1'b0;
                broadcast <= 1'b0;
            end else if (transaction_active && sclk_rise) begin
                if (bit_count < 5'd8)
                    command_shift <= {command_shift[6:0], system_spi_0_io_data_0_OUT};

                if (bit_count == 5'd7) begin
                    command <= {command_shift[6:0], system_spi_0_io_data_0_OUT};
                    if (broadcast_command({command_shift[6:0], system_spi_0_io_data_0_OUT})) begin
                        broadcast <= 1'b1;
                        route_valid <= 1'b1;
                    end else if (!address_command({command_shift[6:0], system_spi_0_io_data_0_OUT})) begin
                        route_flash <= active_flash;
                        route_valid <= 1'b1;
                    end
                end else if (bit_count == 5'd8 && address_command(command)) begin
                    route_flash <= system_spi_0_io_data_0_OUT;
                    active_flash <= system_spi_0_io_data_0_OUT;
                    route_valid <= 1'b1;
                end

                if (bit_count != 5'h1F)
                    bit_count <= bit_count + 1'b1;
            end
        end
    end

    assign flash1_sclk = system_spi_0_io_sclk_write;
    assign flash2_sclk = system_spi_0_io_sclk_write;

    // Both chips receive the opcode.  Addressed commands remain selected
    // through logical A23; after A23 is captured only the selected chip stays
    // active.  Commands without an address use active_flash (power-up bank 0,
    // then the bank selected by the most recent addressed command).
    //
    // Global opcode-only commands keep both CS pins asserted for the complete
    // transaction.  This is required for WREN before a Flash2 erase/program;
    // otherwise the mapper would enable only the previously selected bank.
    // Addressed commands select a bank after A23, while ID/status reads use
    // active_flash and cannot cause MISO contention.
    assign flash1_cs_n = !transaction_active ? 1'b1 :
                         (unsupported_multi_io_command ? 1'b1 :
                         ((!command_known || broadcast ||
                            !route_valid ||
                            current_flash == 1'b0) ?
                          1'b0 : 1'b1));
    assign flash2_cs_n = !transaction_active ? 1'b1 :
                         (unsupported_multi_io_command ? 1'b1 :
                         ((!command_known || broadcast ||
                            !route_valid ||
                            current_flash == 1'b1) ?
                          1'b0 : 1'b1));

    assign flash1_io0_OE = system_spi_0_io_data_0_OE;
    assign flash2_io0_OE = system_spi_0_io_data_0_OE;
    assign flash1_io1_OE = system_spi_0_io_data_1_OE;
    assign flash2_io1_OE = system_spi_0_io_data_1_OE;

    assign flash1_io0_OUT = physical_data0;
    assign flash2_io0_OUT = physical_data0;
    assign flash1_io1_OUT = system_spi_0_io_data_1_OUT;
    assign flash2_io1_OUT = system_spi_0_io_data_1_OUT;

    // 只把当前选中的 Flash 数据回送到逻辑 SPI 端口。
    assign system_spi_0_io_data_0_IN = current_flash ?
                                       flash2_io0_IN : flash1_io0_IN;
    assign system_spi_0_io_data_1_IN = current_flash ?
                                       flash2_io1_IN : flash1_io1_IN;
end
endgenerate

endmodule

`resetall
