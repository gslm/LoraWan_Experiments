cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# LoRaWAN_End_Node_ADR - NUCLEO-WL55JC (STM32WL55JC CPU1/Cortex-M4)
#
# Self-contained copy of ST's STM32CubeWL v1.4.0 example
#   Projects/NUCLEO-WL55JC/Applications/LoRaWAN/LoRaWAN_End_Node
# (LoRaWAN middleware V2.6.2 = Semtech LoRaMac-node, L2 1.0.4 / RP002-1.0.3),
# with lab configuration (AU915 sub-band 2, 15 s uplinks) and ADR tracing.
#
# Sources / include paths / defines mirror the STM32CubeIDE .project/.cproject.
# -----------------------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME LoRaWAN_End_Node_ADR)

# Toolchain is normally given by CMakePresets.json; keep a fallback for plain cmake calls.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} C ASM)

# -----------------------------------------------------------------------------
# Lab knobs. The defaults live in LoRaWAN/App/lab_config.h (single place to edit).
# These cache variables only OVERRIDE the header when set (empty = use header).
#   -DLAB_REGION=AU915|US915|EU868
#   -DLAB_SUBBAND=0..8          (AU915/US915 only; 0 = all 72 channels, no hybrid mask)
#   -DLAB_APP_TX_DUTYCYCLE_MS=15000
#   -DLAB_ADR_TRACE=0|1
# -----------------------------------------------------------------------------
set(LAB_REGION "" CACHE STRING "Override LoRaWAN region (AU915, US915, EU868); empty = lab_config.h default")
set_property(CACHE LAB_REGION PROPERTY STRINGS "" AU915 US915 EU868)
set(LAB_SUBBAND "" CACHE STRING "Override AU915/US915 sub-band 1..8 (0 = all channels); empty = lab_config.h default")
set(LAB_APP_TX_DUTYCYCLE_MS "" CACHE STRING "Override uplink period in ms; empty = lab_config.h default")
set(LAB_ADR_TRACE "" CACHE STRING "Override ADR tracing (0/1); empty = lab_config.h default")

set(LAB_DEFINES "")
if(LAB_REGION)
  list(APPEND LAB_DEFINES LAB_REGION_${LAB_REGION})
endif()
if(NOT LAB_SUBBAND STREQUAL "")
  list(APPEND LAB_DEFINES LAB_SUBBAND=${LAB_SUBBAND})
endif()
if(LAB_APP_TX_DUTYCYCLE_MS)
  list(APPEND LAB_DEFINES LAB_APP_TX_DUTYCYCLE_MS=${LAB_APP_TX_DUTYCYCLE_MS})
endif()
if(NOT LAB_ADR_TRACE STREQUAL "")
  list(APPEND LAB_DEFINES LAB_ADR_TRACE=${LAB_ADR_TRACE})
endif()

add_executable(${CMAKE_PROJECT_NAME})

# ---------------------------------------------------------------- sources ---
set(APP_SOURCES
  Core/Startup/startup_stm32wl55jcix.s
  Core/Src/adc.c
  Core/Src/adc_if.c
  Core/Src/dma.c
  Core/Src/flash_if.c
  Core/Src/gpio.c
  Core/Src/main.c
  Core/Src/rtc.c
  Core/Src/stm32_lpm_if.c
  Core/Src/stm32wlxx_hal_msp.c
  Core/Src/stm32wlxx_it.c
  Core/Src/subghz.c
  Core/Src/sys_app.c
  Core/Src/sys_debug.c
  Core/Src/sys_sensors.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  Core/Src/system_stm32wlxx.c
  Core/Src/timer_if.c
  Core/Src/usart.c
  Core/Src/usart_if.c
  LoRaWAN/App/CayenneLpp.c
  LoRaWAN/App/app_lorawan.c
  LoRaWAN/App/lora_app.c
  LoRaWAN/App/lora_info.c
  LoRaWAN/Target/radio_board_if.c
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LoRaWAN/App/adr_trace.c")
  list(APPEND APP_SOURCES LoRaWAN/App/adr_trace.c)
endif()

set(HAL_DIR Drivers/STM32WLxx_HAL_Driver/Src)
set(DRIVER_SOURCES
  ${HAL_DIR}/stm32wlxx_hal.c
  ${HAL_DIR}/stm32wlxx_hal_adc.c
  ${HAL_DIR}/stm32wlxx_hal_adc_ex.c
  ${HAL_DIR}/stm32wlxx_hal_cortex.c
  ${HAL_DIR}/stm32wlxx_hal_dma.c
  ${HAL_DIR}/stm32wlxx_hal_dma_ex.c
  ${HAL_DIR}/stm32wlxx_hal_exti.c
  ${HAL_DIR}/stm32wlxx_hal_flash.c
  ${HAL_DIR}/stm32wlxx_hal_flash_ex.c
  ${HAL_DIR}/stm32wlxx_hal_gpio.c
  ${HAL_DIR}/stm32wlxx_hal_pwr.c
  ${HAL_DIR}/stm32wlxx_hal_pwr_ex.c
  ${HAL_DIR}/stm32wlxx_hal_rcc.c
  ${HAL_DIR}/stm32wlxx_hal_rcc_ex.c
  ${HAL_DIR}/stm32wlxx_hal_rtc.c
  ${HAL_DIR}/stm32wlxx_hal_rtc_ex.c
  ${HAL_DIR}/stm32wlxx_hal_subghz.c
  ${HAL_DIR}/stm32wlxx_hal_tim.c
  ${HAL_DIR}/stm32wlxx_hal_tim_ex.c
  ${HAL_DIR}/stm32wlxx_hal_uart.c
  ${HAL_DIR}/stm32wlxx_hal_uart_ex.c
  ${HAL_DIR}/stm32wlxx_ll_adc.c
  Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo.c
  Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo_radio.c
)

set(LRW Middlewares/Third_Party/LoRaWAN)
set(LORAWAN_SOURCES
  ${LRW}/LmHandler/LmHandler.c
  ${LRW}/LmHandler/NvmDataMgmt.c
  ${LRW}/LmHandler/Packages/LmhpCompliance.c
  ${LRW}/LmHandler/Packages/LmhpPackagesRegistration.c
  ${LRW}/Mac/LoRaMac.c
  ${LRW}/Mac/LoRaMacAdr.c
  ${LRW}/Mac/LoRaMacClassB.c
  ${LRW}/Mac/LoRaMacCommands.c
  ${LRW}/Mac/LoRaMacConfirmQueue.c
  ${LRW}/Mac/LoRaMacCrypto.c
  ${LRW}/Mac/LoRaMacParser.c
  ${LRW}/Mac/LoRaMacSerializer.c
  ${LRW}/Mac/Region/Region.c
  ${LRW}/Mac/Region/RegionAS923.c
  ${LRW}/Mac/Region/RegionAU915.c
  ${LRW}/Mac/Region/RegionBaseUS.c
  ${LRW}/Mac/Region/RegionCN470.c
  ${LRW}/Mac/Region/RegionCN470A20.c
  ${LRW}/Mac/Region/RegionCN470A26.c
  ${LRW}/Mac/Region/RegionCN470B20.c
  ${LRW}/Mac/Region/RegionCN470B26.c
  ${LRW}/Mac/Region/RegionCN779.c
  ${LRW}/Mac/Region/RegionCommon.c
  ${LRW}/Mac/Region/RegionEU433.c
  ${LRW}/Mac/Region/RegionEU868.c
  ${LRW}/Mac/Region/RegionIN865.c
  ${LRW}/Mac/Region/RegionKR920.c
  ${LRW}/Mac/Region/RegionRU864.c
  ${LRW}/Mac/Region/RegionUS915.c
  ${LRW}/Crypto/cmac.c
  ${LRW}/Crypto/lorawan_aes.c
  ${LRW}/Crypto/soft-se.c
  ${LRW}/Utilities/utilities.c
  Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio.c
  Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_driver.c
  Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_fw.c
)

set(UTILITIES_SOURCES
  Utilities/trace/adv_trace/stm32_adv_trace.c
  Utilities/lpm/tiny_lpm/stm32_lpm.c
  Utilities/misc/stm32_mem.c
  Utilities/sequencer/stm32_seq.c
  Utilities/misc/stm32_systime.c
  Utilities/timer/stm32_timer.c
  Utilities/misc/stm32_tiny_sscanf.c
  Utilities/misc/stm32_tiny_vsnprintf.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  ${APP_SOURCES} ${DRIVER_SOURCES} ${LORAWAN_SOURCES} ${UTILITIES_SOURCES})

# ------------------------------------------------------------ include dirs ---
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  Core/Inc
  LoRaWAN/App
  LoRaWAN/Target
  Drivers/STM32WLxx_HAL_Driver/Inc
  Drivers/STM32WLxx_HAL_Driver/Inc/Legacy
  Utilities/trace/adv_trace
  Utilities/misc
  Utilities/sequencer
  Utilities/timer
  Utilities/lpm/tiny_lpm
  ${LRW}/LmHandler/Packages
  Drivers/CMSIS/Device/ST/STM32WLxx/Include
  ${LRW}/Crypto
  ${LRW}/Mac/Region
  ${LRW}/Mac
  ${LRW}/LmHandler
  ${LRW}/Utilities
  Middlewares/Third_Party/SubGHz_Phy
  Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver
  Drivers/CMSIS/Include
  Drivers/BSP/STM32WLxx_Nucleo
)

# ---------------------------------------------------------------- defines ---
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  CORE_CM4
  USE_HAL_DRIVER
  STM32WL55xx
  $<$<CONFIG:Debug>:DEBUG>
  ${LAB_DEFINES}
)

# Silence the newer-binutils (>= 2.39) "LOAD segment with RWX permissions" note when supported
# (the ST linker script places code and data like every STM32Cube example; harmless).
execute_process(COMMAND ${CMAKE_C_COMPILER} -Wl,--no-warn-rwx-segments -Wl,--version
                RESULT_VARIABLE _rwx_res OUTPUT_QUIET ERROR_QUIET)
if(_rwx_res EQUAL 0)
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,--no-warn-rwx-segments")
endif()

# ------------------------------------------------------- post-build: hex/bin ---
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
  COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex/.bin")
