`timescale 1ns / 1ps

module itch_parser_fsm (
    input  logic        clk,
    input  logic        rst_n,

    input  logic [63:0] s_axis_tdata,
    input  logic [7:0]  s_axis_tkeep,
    input  logic        s_axis_tvalid,
    input  logic        s_axis_tlast,
    output logic        s_axis_tready,

    output logic [63:0] m_order_id,
    output logic [31:0] m_shares,
    output logic [31:0] m_price,
    output logic [7:0]  m_side,
    output logic        m_order_valid,
    input  logic        m_order_ready
);

    typedef enum logic [3:0] {
        ST_IDLE,
        ST_ETH_IP_1,
        ST_IP_2,
        ST_IP_UDP,
        ST_MOLD_HDR_0,
        ST_MOLD_HDR_1,
        ST_ITCH_MSG_0,
        ST_ITCH_MSG_1,
        ST_ITCH_MSG_2,
        ST_ITCH_MSG_3,
        ST_ITCH_MSG_4,
        ST_DROP
    } state_t;

    state_t state, state_next;

    logic [63:0] order_id_reg, order_id_next;
    logic [31:0] shares_reg, shares_next;
    logic [31:0] price_reg, price_next;
    logic [7:0]  side_reg, side_next;
    logic        order_valid_reg, order_valid_next;

    assign s_axis_tready = (state != ST_ITCH_MSG_4) || m_order_ready;
    assign m_order_id    = order_id_reg;
    assign m_shares      = shares_reg;
    assign m_price       = price_reg;
    assign m_side        = side_reg;
    assign m_order_valid = order_valid_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state           <= ST_IDLE;
            order_id_reg    <= 64'd0;
            shares_reg      <= 32'd0;
            price_reg       <= 32'd0;
            side_reg        <= 8'd0;
            order_valid_reg <= 1'b0;
        end else begin
            state           <= state_next;
            order_id_reg    <= order_id_next;
            shares_reg      <= shares_next;
            price_reg       <= price_next;
            side_reg        <= side_next;
            order_valid_reg <= order_valid_next;
        end
    end

    always_comb begin
        state_next       = state;
        order_id_next    = order_id_reg;
        shares_next      = shares_reg;
        price_next       = price_reg;
        side_next        = side_reg;
        order_valid_next = order_valid_reg;

        if (order_valid_reg && m_order_ready) begin
            order_valid_next = 1'b0;
        end

        if (state == ST_ITCH_MSG_4) begin
            if (m_order_ready) begin
                if (s_axis_tvalid) begin
                    if (s_axis_tlast) begin
                        state_next = ST_IDLE;
                    end else begin
                        state_next = ST_ITCH_MSG_0;
                    end
                end
            end
        end else if (s_axis_tvalid && s_axis_tready) begin
            case (state)
                ST_IDLE: begin
                    state_next = ST_ETH_IP_1;
                end

                ST_ETH_IP_1: begin
                    state_next = ST_IP_2;
                end

                ST_IP_2: begin
                    state_next = ST_IP_UDP;
                end

                ST_IP_UDP: begin
                    state_next = ST_MOLD_HDR_0;
                end

                ST_MOLD_HDR_0: begin
                    state_next = ST_MOLD_HDR_1;
                end

                ST_MOLD_HDR_1: begin
                    state_next = ST_ITCH_MSG_0;
                end

                ST_ITCH_MSG_0: begin
                    if (s_axis_tdata[23:16] == 8'h41) begin
                        state_next = ST_ITCH_MSG_1;
                    end else begin
                        state_next = ST_DROP;
                    end
                end

                ST_ITCH_MSG_1: begin
                    order_id_next[63:56] = s_axis_tdata[7:0];
                    order_id_next[55:48] = s_axis_tdata[15:8];
                    order_id_next[47:40] = s_axis_tdata[23:16];
                    order_id_next[39:32] = s_axis_tdata[31:24];
                    order_id_next[31:24] = s_axis_tdata[39:32];
                    order_id_next[23:16] = s_axis_tdata[47:40];
                    order_id_next[15:8]  = s_axis_tdata[55:48];
                    order_id_next[7:0]   = s_axis_tdata[63:56];
                    state_next = ST_ITCH_MSG_2;
                end

                ST_ITCH_MSG_2: begin
                    side_next = s_axis_tdata[7:0];
                    shares_next[31:24] = s_axis_tdata[15:8];
                    shares_next[23:16] = s_axis_tdata[23:16];
                    shares_next[15:8]  = s_axis_tdata[31:24];
                    shares_next[7:0]   = s_axis_tdata[39:32];
                    state_next = ST_ITCH_MSG_3;
                end

                ST_ITCH_MSG_3: begin
                    price_next[31:24] = s_axis_tdata[39:32];
                    price_next[23:16] = s_axis_tdata[47:40];
                    price_next[15:8]  = s_axis_tdata[55:48];
                    price_next[7:0]   = s_axis_tdata[63:56];
                    order_valid_next  = 1'b1;
                    state_next = ST_ITCH_MSG_4;
                end

                ST_DROP: begin
                    if (s_axis_tlast) begin
                        state_next = ST_IDLE;
                    end
                end

                default: state_next = ST_IDLE;
            endcase

            if (s_axis_tlast && state != ST_ITCH_MSG_3 && state != ST_DROP) begin
                state_next = ST_IDLE;
            end
        end
    end

endmodule
