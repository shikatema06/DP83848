cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME DP83848)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/net_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/tcp_echoserver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ADS8354/ads8354.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/ADS8354
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Per-target linker script (the global -T was removed in gcc-arm-none-eabi.cmake
# because it was concatenating with the Bootloader's BOOT.ld and breaking BSS).
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32F407XX_APP.ld
)

# Bootloader target (IAP/TFTP with A/B slots)
add_subdirectory(Bootloader)

# Minimal jump-test application (Milestone 1 — proves Bootloader→App jump)
add_subdirectory(TestApp)

# ---- Slot App builds (A/B/C firmware, distinct version numbers) ----
# Reuses CubeMX application sources + HAL/LwIP, links to a specific slot
# address, and overrides FW_VERSION_* so each image reports a distinct
# version (readable via TCP 1030 command 0x07).
function(add_slot_app NAME LINK_SCRIPT VMAJ VMIN VPATCH)
    add_executable(${NAME})
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ADS8354)
    target_sources(${NAME} PRIVATE
        ${MX_Application_Src}
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/net_config.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/tcp_echoserver.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ADS8354/ads8354.c
    )
    target_compile_definitions(${NAME} PRIVATE
        FW_VERSION_MAJOR=${VMAJ} FW_VERSION_MINOR=${VMIN} FW_VERSION_PATCH=${VPATCH}
    )
    target_link_libraries(${NAME} ${MX_LINK_LIBS})
    target_link_options(${NAME} PRIVATE -T${LINK_SCRIPT})
    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${NAME}>
            $<TARGET_FILE_DIR:${NAME}>/${NAME}.bin)
endfunction()

# app_a: Slot A (0x08010000) v1.0.0 — 当前版本
add_slot_app(app_a ${CMAKE_SOURCE_DIR}/STM32F407XX_APP.ld    1 0 0)
# app_b: Slot B (0x08060000) v2.0.0
add_slot_app(app_b ${CMAKE_SOURCE_DIR}/STM32F407XX_SLOT_B.ld  2 0 0)
# app_c: Slot B (0x08060000) v3.0.0 — 用于 TFTP 升级验证
add_slot_app(app_c ${CMAKE_SOURCE_DIR}/STM32F407XX_SLOT_B.ld  3 0 0)

