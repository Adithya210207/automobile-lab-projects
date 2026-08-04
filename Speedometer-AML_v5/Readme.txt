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

