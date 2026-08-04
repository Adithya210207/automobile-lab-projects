Second prototype
converted ADC values into percentages and showed in UI
and used those gears (3) to change the mode
used F/R button for forward and reverse functionality
Added adaptive cruse control using an push button 
the cruse control turns of when:
1.when that push button is presses again (cruse control button)
2.when brake is press , (here i used a push button to simulate a brake ,kind of a placeholder)
3.After the cruse control button is pressed , when it reached rest , adc =0, if there is a sudden spike of 30% of ADC value 
(step value) within a second.

Third prototype (v6) - Adaptive cruise control (gap control)
Added a Waveshare TOF Laser Range Sensor (D), read over a second software UART
(D7/D8), separate from the existing hardware Serial link to the web UI.
New ACC toggle button on D9, wired the same as the cruise/brake buttons.
ACC is a separate mode from the flat-speed cruise above - only one of the two
can be active at a time, engaging one cancels the other. Brake cancels ACC too.

The Arduino's only job here is reading the sensor (NLink_TOFSense_Frame0
protocol, 16-byte frames) and forwarding ACC=0|1 and DIST=<mm> (-1 = no
target) over serial. All the actual gap control - time-gap target, PD
control, lost-target fallback to a flat hold, and output smoothing - runs
in script.js, driving the simulated vehicle's speed directly instead of
the throttle handle. No physical throttle actuation, by design - this is
meant to validate the whole control loop with zero hardware risk before any
wheel/motor is involved.

