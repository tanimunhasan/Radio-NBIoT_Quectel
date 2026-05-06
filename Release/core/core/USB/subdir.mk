################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\CDC.cpp \
C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\PluggableUSB.cpp \
C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\USBCore.cpp 

C_SRCS += \
C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\samd21_host.c 

C_DEPS += \
.\core\core\USB\samd21_host.c.d 

AR_OBJ += \
.\core\core\USB\CDC.cpp.o \
.\core\core\USB\PluggableUSB.cpp.o \
.\core\core\USB\USBCore.cpp.o \
.\core\core\USB\samd21_host.c.o 

CPP_DEPS += \
.\core\core\USB\CDC.cpp.d \
.\core\core\USB\PluggableUSB.cpp.d \
.\core\core\USB\USBCore.cpp.d 


# Each subdirectory must supply rules for building sources it contributes
core\core\USB\CDC.cpp.o: C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\CDC.cpp
	@echo 'Building file: $<'
	@echo 'Starting C++ compile'
	"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4/bin/arm-none-eabi-g++" -mcpu=cortex-m0plus -mthumb -c -g -Os -w -std=gnu++11 -ffunction-sections -fdata-sections -fno-threadsafe-statics -nostdlib --param max-inline-insns-single=500 -fno-rtti -fno-exceptions -MMD -DF_CPU=48000000L -DARDUINO=10812 -DARDUINO_SAMD_MKRWIFI1010 -DARDUINO_ARCH_SAMD  -DUSE_ARDUINO_MKR_PIN_LAYOUT -D__SAMD21G18A__ -DUSB_VID=0x2341 -DUSB_PID=0x8054 -DUSBCON "-DUSB_MANUFACTURER=\"Arduino LLC\"" "-DUSB_PRODUCT=\"Arduino MKR WiFi 1010\"" -DUSE_BQ24195L_PMIC "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS\4.5.0/CMSIS/Include/" "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS-Atmel\1.2.0/CMSIS/Device/ATMEL/" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\variants\mkrwifi1010" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -D__IN_ECLIPSE__=1 "$<" -o "$@"
	@echo 'Finished building: $<'
	@echo ' '

core\core\USB\PluggableUSB.cpp.o: C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\PluggableUSB.cpp
	@echo 'Building file: $<'
	@echo 'Starting C++ compile'
	"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4/bin/arm-none-eabi-g++" -mcpu=cortex-m0plus -mthumb -c -g -Os -w -std=gnu++11 -ffunction-sections -fdata-sections -fno-threadsafe-statics -nostdlib --param max-inline-insns-single=500 -fno-rtti -fno-exceptions -MMD -DF_CPU=48000000L -DARDUINO=10812 -DARDUINO_SAMD_MKRWIFI1010 -DARDUINO_ARCH_SAMD  -DUSE_ARDUINO_MKR_PIN_LAYOUT -D__SAMD21G18A__ -DUSB_VID=0x2341 -DUSB_PID=0x8054 -DUSBCON "-DUSB_MANUFACTURER=\"Arduino LLC\"" "-DUSB_PRODUCT=\"Arduino MKR WiFi 1010\"" -DUSE_BQ24195L_PMIC "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS\4.5.0/CMSIS/Include/" "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS-Atmel\1.2.0/CMSIS/Device/ATMEL/" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\variants\mkrwifi1010" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -D__IN_ECLIPSE__=1 "$<" -o "$@"
	@echo 'Finished building: $<'
	@echo ' '

core\core\USB\USBCore.cpp.o: C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\USBCore.cpp
	@echo 'Building file: $<'
	@echo 'Starting C++ compile'
	"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4/bin/arm-none-eabi-g++" -mcpu=cortex-m0plus -mthumb -c -g -Os -w -std=gnu++11 -ffunction-sections -fdata-sections -fno-threadsafe-statics -nostdlib --param max-inline-insns-single=500 -fno-rtti -fno-exceptions -MMD -DF_CPU=48000000L -DARDUINO=10812 -DARDUINO_SAMD_MKRWIFI1010 -DARDUINO_ARCH_SAMD  -DUSE_ARDUINO_MKR_PIN_LAYOUT -D__SAMD21G18A__ -DUSB_VID=0x2341 -DUSB_PID=0x8054 -DUSBCON "-DUSB_MANUFACTURER=\"Arduino LLC\"" "-DUSB_PRODUCT=\"Arduino MKR WiFi 1010\"" -DUSE_BQ24195L_PMIC "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS\4.5.0/CMSIS/Include/" "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS-Atmel\1.2.0/CMSIS/Device/ATMEL/" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\variants\mkrwifi1010" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -D__IN_ECLIPSE__=1 "$<" -o "$@"
	@echo 'Finished building: $<'
	@echo ' '

core\core\USB\samd21_host.c.o: C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino\USB\samd21_host.c
	@echo 'Building file: $<'
	@echo 'Starting C compile'
	"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4/bin/arm-none-eabi-gcc" -mcpu=cortex-m0plus -mthumb -c -g -Os -w -std=gnu11 -ffunction-sections -fdata-sections -nostdlib --param max-inline-insns-single=500 -MMD -DF_CPU=48000000L -DARDUINO=10812 -DARDUINO_SAMD_MKRWIFI1010 -DARDUINO_ARCH_SAMD  -DUSE_ARDUINO_MKR_PIN_LAYOUT -D__SAMD21G18A__ -DUSB_VID=0x2341 -DUSB_PID=0x8054 -DUSBCON "-DUSB_MANUFACTURER=\"Arduino LLC\"" "-DUSB_PRODUCT=\"Arduino MKR WiFi 1010\"" -DUSE_BQ24195L_PMIC "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS\4.5.0/CMSIS/Include/" "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS-Atmel\1.2.0/CMSIS/Device/ATMEL/" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\variants\mkrwifi1010" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -D__IN_ECLIPSE__=1 "$<" -o "$@"
	@echo 'Finished building: $<'
	@echo ' '


