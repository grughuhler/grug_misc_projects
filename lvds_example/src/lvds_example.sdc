//Copyright (C)2014-2025 GOWIN Semiconductor Corporation.
//All rights reserved.
//File Title: Timing Constraints file
//Tool Version: V1.9.11.03 (64-bit) 
//Created Time: 2025-11-05 15:00:03
create_clock -name clk_in -period 37.037 -waveform {0 18.518} [get_ports {clk_in}] -add
create_clock -name clk_div1 -period 185.185 -waveform {0 92.593} [get_nets {clk_div1}] -add

