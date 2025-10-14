################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/game.c \
../Src/hardware.c \
../Src/main.c \
../Src/oled.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/utils.c 

OBJS += \
./Src/game.o \
./Src/hardware.o \
./Src/main.o \
./Src/oled.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/utils.o 

C_DEPS += \
./Src/game.d \
./Src/hardware.d \
./Src/main.d \
./Src/oled.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/utils.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DNUCLEO_F411RE -DSTM32 -DSTM32F4 -DSTM32F411RETx -c -I../Inc -I"C:/Users/KKU11/Downloads/Library/CMSIS/Core/Include" -I"C:/Users/KKU11/Downloads/Library/CMSIS-DEVICE-F4/Include" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/game.cyclo ./Src/game.d ./Src/game.o ./Src/game.su ./Src/hardware.cyclo ./Src/hardware.d ./Src/hardware.o ./Src/hardware.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/oled.cyclo ./Src/oled.d ./Src/oled.o ./Src/oled.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/utils.cyclo ./Src/utils.d ./Src/utils.o ./Src/utils.su

.PHONY: clean-Src

