# Toolchain file for arm-none-eabi-gcc (STM32WL55JC, Cortex-M4, no FPU).
#
# Compiler lookup order:
#   1. -DARM_TOOLCHAIN_DIR=<dir containing bin/arm-none-eabi-gcc> (or env ARM_TOOLCHAIN_DIR)
#   2. arm-none-eabi-gcc found on PATH (this is what the STM32 VS Code extension
#      provides through cube-cmake / the gnu-tools-for-stm32 bundle)
#   3. the STM32Cube bundle in ~/.local/share/stm32cube/bundles/gnu-tools-for-stm32/*
set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
  set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()
if(NOT ARM_TOOLCHAIN_DIR)
  find_program(_ARM_GCC_ON_PATH ${TOOLCHAIN_PREFIX}gcc)
  if(NOT _ARM_GCC_ON_PATH)
    file(GLOB _ST_BUNDLE_GCC "$ENV{HOME}/.local/share/stm32cube/bundles/gnu-tools-for-stm32/*/bin/${TOOLCHAIN_PREFIX}gcc")
    if(_ST_BUNDLE_GCC)
      list(SORT _ST_BUNDLE_GCC)
      list(GET _ST_BUNDLE_GCC -1 _ST_BUNDLE_GCC)
      get_filename_component(ARM_TOOLCHAIN_DIR "${_ST_BUNDLE_GCC}" DIRECTORY)
      get_filename_component(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" DIRECTORY)
    endif()
  endif()
endif()
set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH "Directory holding bin/arm-none-eabi-gcc (empty = use PATH)")

if(ARM_TOOLCHAIN_DIR)
  set(_TC_BIN "${ARM_TOOLCHAIN_DIR}/bin/")
else()
  set(_TC_BIN "")
endif()

set(CMAKE_C_COMPILER                ${_TC_BIN}${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${_TC_BIN}${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${_TC_BIN}${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${_TC_BIN}${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${_TC_BIN}${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU flags (STM32WL55JC CPU1 = Cortex-M4 without FPU, as in the STM32CubeIDE project)
set(TARGET_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -Wall -fdata-sections -ffunction-sections -fstack-usage")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")

# Debug = STM32CubeIDE "Debug" configuration (-Og -g3), Release = "Release" (-Os)
set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_ASM_FLAGS_DEBUG "-g3")
set(CMAKE_ASM_FLAGS_RELEASE "-g0")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32WL55JCIX_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs --specs=nosys.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections -static")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--start-group -lc -lm -Wl,--end-group")
