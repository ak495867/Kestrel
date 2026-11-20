create_project -force itch_parser_synth ./build_synth -part xcvu9p-flga2104-2L-e
read_verilog -sv hardware/itch_parser_fsm.sv
synth_design -top itch_parser_fsm -part xcvu9p-flga2104-2L-e -mode out_of_context
create_clock -period 3.103 -name clk [get_ports clk]
opt_design
place_design
route_design
report_timing_summary -file hardware/timing_summary.rpt
report_utilization -file hardware/utilization.rpt
report_power -file hardware/power.rpt
