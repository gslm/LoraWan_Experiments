#!/usr/bin/env bash
# Assemble a self-contained copy of ST's NUCLEO-WL55JC LoRaWAN_End_Node example
# (STM32CubeWL v1.4.0, last release with the LoRaMac-node based "LoRaWAN" middleware)
# into <dest>, keeping the upstream relative layout:
#   <dest>/{Core,LoRaWAN,Drivers,Middlewares,Utilities}
#
# Used for two things:
#   1. initial creation of firmware/LoRaWAN_End_Node_ADR (then modified by hand),
#   2. re-creating a PRISTINE tree to diff against (tools/gen_adr_trace_diff.sh).
#
# usage: assemble_from_cube.sh <STM32CubeWL root> <dest dir>
set -euo pipefail

SRC=${1:?STM32CubeWL root (e.g. third_party/STM32CubeWL)}
DST=${2:?destination directory}
EX=$SRC/Projects/NUCLEO-WL55JC/Applications/LoRaWAN/LoRaWAN_End_Node

[ -d "$EX" ] || { echo "example not found: $EX" >&2; exit 1; }
mkdir -p "$DST"
cd "$DST"

# --- application (CubeMX generated part + LoRaWAN app) ---------------------
cp -r "$EX/Core" "$EX/LoRaWAN" .
cp "$EX/readme.txt" readme_upstream.txt
cp "$EX/LoRaWAN_End_Node.ioc" LoRaWAN_End_Node_upstream.ioc
mkdir -p Core/Startup
cp "$EX/STM32CubeIDE/Application/User/Startup/startup_stm32wl55jcix.s" Core/Startup/
cp "$EX/STM32CubeIDE/Application/User/Core/syscalls.c" \
   "$EX/STM32CubeIDE/Application/User/Core/sysmem.c" Core/Src/
cp "$EX/STM32CubeIDE/STM32WL55JCIX_FLASH.ld" .

# --- drivers ---------------------------------------------------------------
mkdir -p Drivers/CMSIS/Device/ST/STM32WLxx Drivers/STM32WLxx_HAL_Driver/Src Drivers/BSP/STM32WLxx_Nucleo
cp -r "$SRC/Drivers/CMSIS/Include" Drivers/CMSIS/
cp "$SRC/Drivers/CMSIS/LICENSE.txt" Drivers/CMSIS/
cp -r "$SRC/Drivers/CMSIS/Device/ST/STM32WLxx/Include" Drivers/CMSIS/Device/ST/STM32WLxx/
cp "$SRC"/Drivers/CMSIS/Device/ST/STM32WLxx/LICENSE* Drivers/CMSIS/Device/ST/STM32WLxx/
cp -r "$SRC/Drivers/STM32WLxx_HAL_Driver/Inc" Drivers/STM32WLxx_HAL_Driver/
cp "$SRC"/Drivers/STM32WLxx_HAL_Driver/LICENSE* Drivers/STM32WLxx_HAL_Driver/
# only the HAL/LL sources referenced by the STM32CubeIDE .project
for f in hal hal_adc hal_adc_ex hal_cortex hal_dma hal_dma_ex hal_exti hal_flash \
         hal_flash_ex hal_gpio hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex hal_rtc hal_rtc_ex \
         hal_subghz hal_tim hal_tim_ex hal_uart hal_uart_ex ll_adc; do
  cp "$SRC/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_$f.c" Drivers/STM32WLxx_HAL_Driver/Src/
done
cp "$SRC"/Drivers/BSP/STM32WLxx_Nucleo/*.c "$SRC"/Drivers/BSP/STM32WLxx_Nucleo/*.h \
   "$SRC"/Drivers/BSP/STM32WLxx_Nucleo/LICENSE* Drivers/BSP/STM32WLxx_Nucleo/

# --- middlewares -------------------------------------------------------------
mkdir -p Middlewares/Third_Party
cp -r "$SRC/Middlewares/Third_Party/LoRaWAN" "$SRC/Middlewares/Third_Party/SubGHz_Phy" Middlewares/Third_Party/

# --- utilities ---------------------------------------------------------------
mkdir -p Utilities/trace Utilities/lpm
cp -r "$SRC/Utilities/trace/adv_trace" Utilities/trace/
cp -r "$SRC/Utilities/lpm/tiny_lpm" Utilities/lpm/
cp -r "$SRC/Utilities/misc" "$SRC/Utilities/sequencer" "$SRC/Utilities/timer" Utilities/

# drop HTML release-note images (not needed to build)
find . -name _htmresc -type d -prune -exec rm -rf {} +
echo "assembled pristine LoRaWAN_End_Node into $DST"
