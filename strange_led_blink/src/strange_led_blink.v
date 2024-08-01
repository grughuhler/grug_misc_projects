/* Copyright 2024 Grug Huhler.  License SPDX BSD-2-Clause.
 *
 * See README
 */

module strange_led_block (
  output wire led,
  output wire pll_clk_out,
  output wire pll_clkd_out
);


wire osc_to_pll;
wire div1_to_div2;
wire div2_to_div3;
wire div3_to_div4;
wire div4_to_div5;
wire div5_to_div6;

Gowin_OSC osc (.oscout(osc_to_pll));

Gowin_CLKDIV clk_div1 (
        .clkout(div1_to_div2),
        .hclkin(pll_clkd_out),
        .resetn(1'b1)
);

Gowin_CLKDIV clk_div2 (
        .clkout(div2_to_div3),
        .hclkin(div1_to_div2),
        .resetn(1'b1)
);

Gowin_CLKDIV clk_div3 (
        .clkout(div3_to_div4),
        .hclkin(div2_to_div3),
        .resetn(1'b1)
);

Gowin_CLKDIV clk_div4 (
        .clkout(div4_to_div5),
        .hclkin(div3_to_div4),
        .resetn(1'b1)
);

Gowin_CLKDIV clk_div5 (
        .clkout(div5_to_div6),
        .hclkin(div4_to_div5),
        .resetn(1'b1)
);

Gowin_CLKDIV clk_div6 (
        .clkout(led_clk),
        .hclkin(div5_to_div6),
        .resetn(1'b1)
);

Gowin_rPLL fred (
        .clkout(pll_clk_out),
        .clkoutd(pll_clkd_out),
        .clkin(osc_to_pll)
);


assign led = led_clk;

endmodule
