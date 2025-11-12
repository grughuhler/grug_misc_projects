// Copyright 2025 Grug Huhler
// License: SPDX BSD-2-Clause
//
// 1. Output a bit pattern via a true LVDS output buffer which connects to
//    a true LVDS input buffer via external cable.
// 2. Take what is read from the LVDS input buffer and output it onto a
//    normal single-ended output pin.
// 3. Use a clock divider to divide clk_in by 5 to make it easy
//    to view external signals on low-end oscilloscopes.

module lvds_receiver_top (
  input  wire clk_in,
  output wire lvds_out_p,  // Differential Input P-side (I)
  output wire lvds_out_n,  // Differential Input N-side (IB)
  output wire single_out,
  input  wire lvds_in_p,   // Differential Input P-side (I)
  input  wire lvds_in_n    // Differential Input N-side (IB)
);

  wire clk_div1; // clk_in div by 5 so 5.4 MHz
  reg reset_n = 1'b0;
  reg [7:0] bit_pattern = 8'b11001010;

  Gowin_CLKDIV div1 (
    .clkout(clk_div1),
    .hclkin(clk_in),
    .resetn(1'b1)
  );

  always @(posedge clk_in)
    if (!reset_n) reset_n <= 1'b1;

  always @(posedge clk_div1)
    if (reset_n) bit_pattern <= {bit_pattern[6:0], bit_pattern[7]};  // shift bits

  TLVDS_OBUF output_buffer (
    .I   (bit_pattern[7]),
    .O   (lvds_out_p),
    .OB  (lvds_out_n)
  );

  TLVDS_IBUF input_buffer (
    .O   (single_out),
    .I   (lvds_in_p),
    .IB  (lvds_in_n)
  );

endmodule // lvds_receiver_top
