################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
..\sloeber.ino.cpp 

LINK_OBJ += \
.\sloeber.ino.cpp.o 

CPP_DEPS += \
.\sloeber.ino.cpp.d 


# Each subdirectory must supply rules for building sources it contributes
sloeber.ino.cpp.o: ..\sloeber.ino.cpp
	@echo 'Building file: $<'
	@echo 'Starting C++ compile'
	"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\arm-none-eabi-gcc\7-2017q4/bin/arm-none-eabi-g++" -mcpu=cortex-m0plus -mthumb -c -g -Os -w -std=gnu++11 -ffunction-sections -fdata-sections -fno-threadsafe-statics -nostdlib --param max-inline-insns-single=500 -fno-rtti -fno-exceptions -MMD -DF_CPU=48000000L -DARDUINO=10812 -DARDUINO_SAMD_MKRWIFI1010 -DARDUINO_ARCH_SAMD  -DUSE_ARDUINO_MKR_PIN_LAYOUT -D__SAMD21G18A__ -DUSB_VID=0x2341 -DUSB_PID=0x8054 -DUSBCON "-DUSB_MANUFACTURER=\"Arduino LLC\"" "-DUSB_PRODUCT=\"Arduino MKR WiFi 1010\"" -DUSE_BQ24195L_PMIC "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS\4.5.0/CMSIS/Include/" "-IC:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\tools\CMSIS-Atmel\1.2.0/CMSIS/Device/ATMEL/" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\cores\arduino" -I"C:\Users\B4T\Downloads\sloeber-ide-V4.4.3-win32.win32.x86_64\Sloeber\arduinoPlugin\packages\arduino\hardware\samd\1.8.9\variants\mkrwifi1010" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -D__IN_ECLIPSE__=1 "$<" -o "$@"

	@echo 'Finished building: $<'
	@echo ' '


