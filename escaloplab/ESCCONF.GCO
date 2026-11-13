; Escaloplab settings update
; Copy this file to the top folder of the SD card, keep the name ESCCONF.GCO,
; then on the printer choose:  Escaloplab > Update from SD card
; Lines starting with ; are notes and are ignored by the printer.

; Steps per mm (X, Y, Z, extruder)
M92 X160 Y160 Z800 E186

; Top speeds in mm/s
M203 X200 Y200 Z5 E50

; Top accelerations in mm/s2
M201 X2000 Y2000 Z100 E5000

; Normal accelerations: printing, retract, travel
M204 P1000 R1500 T1500

; Bed heater tuning (from M303 E-1 S60 C8)
M304 P39.87 I7.73 D137.18

; Hotend heater tuning: after running M303 E0 S210 C8, put the numbers
; here and remove the ; at the start of the next line
;M301 P22.20 I1.08 D114.00

; Save everything so it survives a restart
M500

; Two beeps and a message when done
M300 S1800 P150
M300 S2400 P200
M117 Settings updated
