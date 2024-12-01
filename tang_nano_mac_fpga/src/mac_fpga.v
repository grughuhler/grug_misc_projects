/* mac_fpga top level module
 *
 * License SPDX: BSD-2-Clause
 *
 * This version uses two unidirectional signals.  Use mac_command
 * with it.  Note that some pin choices differ between the versions.
 */

module mac_fpga
   (
    input wire [1:0]  cmd,
    input wire 	      clk,
    input wire 	      din,
    output wire       dout
    //output wire [5:0] leds
    );

   parameter WIDTH = 16;

   wire [(WIDTH-1):0] a_out;
   wire [(WIDTH-1):0] b_out;
   wire [(2*WIDTH-1):0] sum_in;
   wire [(2*WIDTH-1):0] sum_out;
   wire 		reset;
   wire 		ab_ena;
   wire 		ab_clk;
   wire 		c_ena;
   wire 		c_clk;
   wire 		p_ld;
   
   sipo_sr #(.WIDTH(WIDTH)) a
     (.clk(ab_clk),
      .reset(reset),
      .s_in(din),
      .p_out(a_out)
      );

   sipo_sr #(.WIDTH(WIDTH)) b
     (.clk(ab_clk),
      .reset(reset),
      .s_in(a_out[0]),
      .p_out(b_out)
      );

   pipo_sr #(.WIDTH(2*WIDTH)) c
     (.clk(c_clk),
      .reset(reset),
      .p_ld(p_ld),
      .s_in(sum_in[2*WIDTH - 1]),
      .p_in(sum_out),
      .p_out(sum_in)
      );

   // Control signals
   assign ab_ena = ~cmd[1];
   assign c_ena = cmd[1] | ~cmd[0];
   assign p_ld = cmd[1]&(~cmd[0]);
   assign reset = ~(cmd[1] | cmd[0]);

   // Clock gates
   assign ab_clk = ab_ena & clk;
   assign c_clk = c_ena & clk;
 
   // arithmetic  
   assign sum_out = a_out*b_out + sum_in;

   // Output
   assign dout = sum_in[31];
   
   // Debug stuff
   // assign leds[5:3] = a_out[2:0];
   // assign leds[2:0] = b_out[2:0];
   //assign leds[5:3] = sum_out[2:0];
   //assign leds[2:0] = sum_in[2:0];

endmodule // mac_fpga
