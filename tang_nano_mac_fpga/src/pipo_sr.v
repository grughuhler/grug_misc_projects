/* pipo_sr
 *
 * License SPDX: BSD-2-Clause
 * 
 * Parallel to parallel logical left shift register. On rising edge...
 * If p_ld == 1, load from p_in.
 * If clr == 1, set to zero.
 * else shift in bit from s_in.
 */

module pipo_sr #(parameter WIDTH=16)
   (
    input wire 		     clk,
    input wire 		     reset,
    input wire 		     p_ld,
    input wire 		     s_in,
    input wire [(WIDTH-1):0] p_in,
    output reg [(WIDTH-1):0] p_out
    );

   always @(posedge clk)
     if (reset) p_out = 'b0;
     else if (p_ld) p_out = p_in;
     else p_out = {p_out[(WIDTH-2):0], s_in};

endmodule // pipo_sr
