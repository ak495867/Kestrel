`timescale 1ns / 1ps

module tb_itch_parser_fsm;
    logic        clk;
    logic        rst_n;

    logic [63:0] s_axis_tdata;
    logic [7:0]  s_axis_tkeep;
    logic        s_axis_tvalid;
    logic        s_axis_tlast;
    logic        s_axis_tready;

    logic [63:0] m_order_id;
    logic [31:0] m_shares;
    logic [31:0] m_price;
    logic [7:0]  m_side;
    logic        m_order_valid;
    logic        m_order_ready;

    itch_parser_fsm dut (
        .clk(clk),
        .rst_n(rst_n),
        .s_axis_tdata(s_axis_tdata),
        .s_axis_tkeep(s_axis_tkeep),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tlast(s_axis_tlast),
        .s_axis_tready(s_axis_tready),
        .m_order_id(m_order_id),
        .m_shares(m_shares),
        .m_price(m_price),
        .m_side(m_side),
        .m_order_valid(m_order_valid),
        .m_order_ready(m_order_ready)
    );

    initial begin
        clk = 0;
        forever #1.562 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        s_axis_tdata = 64'd0;
        s_axis_tkeep = 8'hFF;
        s_axis_tvalid = 0;
        s_axis_tlast = 0;
        m_order_ready = 0;

        #20;
        rst_n = 1;
        #10;

        @(posedge clk);
        s_axis_tvalid = 1;
        s_axis_tdata  = 64'h0000000000000000;
        @(posedge clk);
        s_axis_tdata  = 64'h0000000000000800;
        @(posedge clk);
        s_axis_tdata  = 64'h0000000000000011;
        @(posedge clk);
        s_axis_tdata  = 64'h0000000000000000;
        @(posedge clk);
        s_axis_tdata  = 64'h0000000000000000;
        @(posedge clk);
        s_axis_tdata  = 64'h0000000000000001;
        @(posedge clk);
        s_axis_tdata  = 64'h0000410024000000;
        @(posedge clk);
        s_axis_tdata  = 64'hEFCDAB8967452301;
        @(posedge clk);
        s_axis_tdata  = 64'h0000006442000000;
        @(posedge clk);
        s_axis_tdata  = 64'h000000004141504C;
        s_axis_tlast  = 1;

        @(posedge clk);
        s_axis_tvalid = 0;
        s_axis_tlast  = 0;

        repeat (5) begin
            @(posedge clk);
            if (!m_order_valid) begin
                $display("[-] Assertion Failed: m_order_valid dropped under backpressure");
                $fatal(1);
            end
        end

        m_order_ready = 1;
        @(posedge clk);

        if (m_order_id != 64'h0123456789ABCDEF) begin
            $display("[-] Order ID mismatch: %h", m_order_id);
            $fatal(1);
        end
        if (m_side != 8'h42) begin
            $display("[-] Side mismatch: %h", m_side);
            $fatal(1);
        end
        if (m_shares != 32'd100) begin
            $display("[-] Shares mismatch: %d", m_shares);
            $fatal(1);
        end

        $display("[+] Hardware FSM backpressure handshake and parser verification PASSED");
        #20;
        $finish;
    end

endmodule
