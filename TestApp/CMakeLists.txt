#
# TestApp CMakeLists — Milestone 1: minimal jump-test application.
#
# A tiny App (link at Slot A 0x08010000) that prints "APP OK" and blinks
# PE7. Used ONLY to prove the Bootloader → App jump is clean before the
# real 92KB App is re-introduced.
#

add_executable(test_app)

target_include_directories(test_app PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc
)

target_sources(test_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_SOURCE_DIR}/Core/Src/gpio.c
    ${CMAKE_SOURCE_DIR}/Core/Src/usart.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f407xx.s
)

# ---- Link at Slot A ----
target_link_libraries(test_app PRIVATE
    STM32_Drivers      # shared HAL objects
)

target_link_options(test_app PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32F407XX_APP.ld
)

# Emit .bin for direct flashing
add_custom_command(TARGET test_app POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:test_app>
        $<TARGET_FILE_DIR:test_app>/test_app.bin
)

# ---- test_app_B: same minimal test app, linked to Slot B (0x08060000).
#      Used as the "new firmware" for TFTP spare-slot upgrade verification.
add_executable(test_app_B)

target_include_directories(test_app_B PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc
)

target_sources(test_app_B PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_SOURCE_DIR}/Core/Src/gpio.c
    ${CMAKE_SOURCE_DIR}/Core/Src/usart.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f407xx.s
)

target_link_libraries(test_app_B PRIVATE
    STM32_Drivers
)

target_link_options(test_app_B PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32F407XX_SLOT_B.ld
)

add_custom_command(TARGET test_app_B POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:test_app_B>
        $<TARGET_FILE_DIR:test_app_B>/test_app_B.bin
)
