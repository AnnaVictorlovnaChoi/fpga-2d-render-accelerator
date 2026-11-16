// Stand-alone read-only test for the second external SPI flash.
//
// The module does not erase or program the device.  It reads JEDEC ID (9F)
// and the status register (05), which makes it safe to use on a populated
// board.  flash_io0 is MOSI/DI and flash_io1 is MISO/DO.
module testflash #(
    parameter integer CLK_HZ = 100_000_000,
    parameter integer SPI_HZ = 1_000_000,
    parameter integer STARTUP_CYCLES = 1_000
)(
    input  wire       clk,
    input  wire       rst_n,

    output reg        flash_sclk,
    output reg        flash_cs_n,
    output reg        flash_io0_oe,
    output reg        flash_io0_out,
    input  wire       flash_io0_in,
    output reg        flash_io1_oe,
    output reg        flash_io1_out,
    input  wire       flash_io1_in,

    output reg [7:0]  led
);

    localparam integer HALF_DIV_RAW = CLK_HZ / (2 * SPI_HZ);
    localparam integer HALF_DIV = (HALF_DIV_RAW < 1) ? 1 : HALF_DIV_RAW;
    localparam integer STARTUP_W = (STARTUP_CYCLES < 2) ? 1 : $clog2(STARTUP_CYCLES + 1);

    localparam [3:0] ST_STARTUP  = 4'd0;
    localparam [3:0] ST_ID_CMD   = 4'd1;
    localparam [3:0] ST_ID_READ  = 4'd2;
    localparam [3:0] ST_STATUS   = 4'd3;
    localparam [3:0] ST_GOOD     = 4'd4;
    localparam [3:0] ST_BAD      = 4'd5;

    reg [3:0] state;
    reg [STARTUP_W-1:0] startup_count;
    reg [31:0] div_count;

    reg        spi_active;
    reg        spi_done;
    reg [7:0]  spi_tx;
    reg [7:0]  spi_rx;
    reg [7:0]  tx_shift;
    reg [7:0]  rx_shift;
    reg [2:0]  bit_count;
    reg        spi_start;

    reg [1:0]  id_index;
    reg [7:0]  jedec_mfg;
    reg [7:0]  jedec_type;
    reg [7:0]  jedec_capacity;
    reg [7:0]  status_reg;

    wire spi_tick = (div_count == HALF_DIV-1);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= ST_STARTUP;
            startup_count  <= {STARTUP_W{1'b0}};
            div_count      <= 32'd0;
            spi_active     <= 1'b0;
            spi_done       <= 1'b0;
            spi_start      <= 1'b0;
            spi_tx         <= 8'h00;
            spi_rx         <= 8'h00;
            tx_shift       <= 8'h00;
            rx_shift       <= 8'h00;
            bit_count      <= 3'd0;
            id_index       <= 2'd0;
            jedec_mfg      <= 8'h00;
            jedec_type     <= 8'h00;
            jedec_capacity <= 8'h00;
            status_reg     <= 8'h00;
            flash_sclk     <= 1'b0;
            flash_cs_n     <= 1'b1;
            flash_io0_oe   <= 1'b0;
            flash_io0_out  <= 1'b0;
            flash_io1_oe   <= 1'b0;
            flash_io1_out  <= 1'b0;
            led            <= 8'b1010_1010;
        end else begin
            spi_done  <= 1'b0;
            spi_start <= 1'b0;

            if (spi_tick)
                div_count <= 32'd0;
            else
                div_count <= div_count + 1'b1;

            // Byte-level SPI engine, mode 0: change data on falling edge,
            // sample data on rising edge.
            if (spi_start && !spi_active) begin
                spi_active    <= 1'b1;
                flash_sclk    <= 1'b0;
                bit_count     <= 3'd0;
                tx_shift      <= spi_tx;
                rx_shift      <= 8'h00;
                flash_io0_out <= spi_tx[7];
            end else if (spi_active && spi_tick) begin
                if (!flash_sclk) begin
                    flash_sclk <= 1'b1;
                    rx_shift   <= {rx_shift[6:0], flash_io1_in};
                end else begin
                    flash_sclk <= 1'b0;
                    if (bit_count == 3'd7) begin
                        spi_active <= 1'b0;
                        spi_done   <= 1'b1;
                        spi_rx     <= rx_shift;
                    end else begin
                        bit_count     <= bit_count + 1'b1;
                        tx_shift      <= {tx_shift[6:0], 1'b0};
                        flash_io0_out <= tx_shift[6];
                    end
                end
            end

            case (state)
                ST_STARTUP: begin
                    flash_cs_n   <= 1'b1;
                    flash_sclk   <= 1'b0;
                    flash_io0_oe <= 1'b0;
                    led           <= 8'b1010_1010;
                    if (startup_count == STARTUP_CYCLES-1) begin
                        state        <= ST_ID_CMD;
                        flash_cs_n   <= 1'b0;
                        flash_io0_oe <= 1'b1;
                    end else begin
                        startup_count <= startup_count + 1'b1;
                    end
                end

                ST_ID_CMD: begin
                    if (!spi_active && !spi_done) begin
                        spi_tx    <= 8'h9F;
                        spi_start <= 1'b1;
                    end
                    if (spi_done) begin
                        id_index <= 2'd0;
                        state    <= ST_ID_READ;
                    end
                end

                ST_ID_READ: begin
                    if (!spi_active && !spi_done) begin
                        spi_tx    <= 8'h00;
                        spi_start <= 1'b1;
                    end
                    if (spi_done) begin
                        case (id_index)
                            2'd0: jedec_mfg      <= spi_rx;
                            2'd1: jedec_type     <= spi_rx;
                            default: jedec_capacity <= spi_rx;
                        endcase
                        if (id_index == 2'd2) begin
                            flash_cs_n <= 1'b1;
                            state      <= ST_STATUS;
                        end else begin
                            id_index <= id_index + 1'b1;
                        end
                    end
                end

                ST_STATUS: begin
                    // A fresh transaction avoids violating CS high time.
                    if (flash_cs_n) begin
                        flash_cs_n   <= 1'b0;
                        flash_io0_oe <= 1'b1;
                    end else if (!spi_active && !spi_done && !status_reg[7]) begin
                        spi_tx    <= 8'h05;
                        spi_start <= 1'b1;
                    end else if (spi_done && !status_reg[7]) begin
                        status_reg[7:0] <= spi_rx;
                        flash_cs_n      <= 1'b1;
                        flash_io0_oe    <= 1'b0;
                        // Winbond W25Q devices normally report EF/60/17 or
                        // EF/60/18.  Accept the common EF40 variants too.
                        if ((jedec_mfg == 8'hEF) &&
                            ((jedec_capacity == 8'h17) ||
                             (jedec_capacity == 8'h18)))
                            state <= ST_GOOD;
                        else
                            state <= ST_BAD;
                    end
                end

                ST_GOOD: begin
                    flash_cs_n   <= 1'b1;
                    flash_sclk   <= 1'b0;
                    flash_io0_oe <= 1'b0;
                    led           <= 8'b1111_1111;
                end

                default: begin // ST_BAD
                    flash_cs_n   <= 1'b1;
                    flash_sclk   <= 1'b0;
                    flash_io0_oe <= 1'b0;
                    led           <= 8'b0000_0001;
                end
            endcase

            flash_io1_oe  <= 1'b0;
            flash_io1_out <= 1'b0;
        end
    end
endmodule
