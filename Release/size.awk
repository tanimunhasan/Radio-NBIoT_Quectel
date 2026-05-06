/^(\.text|\.data)/ {arduino_size += $2 }
/^(\.data|\.bss)/ {arduino_data += $2 }
// {arduino_eeprom += $2 }
END { print "\nSketch uses " arduino_size " bytes (" int(arduino_size/262144*100+0.5)  "%)  of program storage space. Maximum is " 262144 " bytes.\nGlobal variables use "arduino_data" bytes ("int(arduino_data/32768*100+0.5)"%) of dynamic memory, leaving "32768-arduino_data" bytes for local variables. Maximum is "32768" bytes.\n"}