// Copyright 2025 Grug Huhler
// License: SPDX BSD-2-Clause
//
// 1. Output a bit pattern via a true LVDS output buffer
// 2. Use a clock divider to divide clk_in by 5 to make it easy
//    to view external signals on low-end oscilloscopes.

module lvds_receiver_top (
  input  wire clk_in,      // 27 MHz
  output wire lvds_out_p,  // Differential Input P-side (I)
  output wire lvds_out_n   // Differential Input N-side (IB)
);

  wire clk_div1; // clk_in div by 5 so 5.4 Mhz
  reg reset_n = 1'b0;
  reg [7:0] bit_pattern = 8'b11001010;

  Gowin_CLKDIV div1 (
    .clkout(clk_div1),
    .hclkin(clk_in),
    .resetn(reset_n)
  );

  always @(posedge clk_in)
    if (!reset_n) reset_n <= 1'b1;

  always @(posedge clk_div1)
    if (reset_n)
      bit_pattern <= {bit_pattern[6:0], bit_pattern[7]};  // shift bits

  TLVDS_OBUF output_buffer (
    .I   (bit_pattern[7]),
    .O   (lvds_out_p),
    .OB  (lvds_out_n)
  );

endmodule
