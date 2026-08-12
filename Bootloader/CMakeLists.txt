#
# Bootloader CMakeLists — Milestone 3c: minimal jump bootloader + lwIP/TFTP IAP.
#
# Bootloader flow:
#   NORMAL  → iap_slot → jump to active App slot
#   UPGRADE → lwIP (Entry IP) + TFTP server, wait for firmware → commit → reset
#
# flash_if.c / iap_slot.c provide the slot state machine; tftp_server.c writes
# the spare slot and resets on completion.
#

set(LWIP_DIR ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LwIP/src)

# ---- Bootloader-specific LwIP (UDP + ICMP + ARP only, no TCP) ----
set(LwIP_Boot_Src
    ${LWIP_DIR}/core/init.c
    ${LWIP_DIR}/core/def.c
    ${LWIP_DIR}/core/inet_chksum.c
    ${LWIP_DIR}/core/ip.c
    ${LWIP_DIR}/core/mem.c
    ${LWIP_DIR}/core/memp.c
    ${LWIP_DIR}/core/netif.c
    ${LWIP_DIR}/core/pbuf.c
    ${LWIP_DIR}/core/stats.c
    ${LWIP_DIR}/core/sys.c
    ${LWIP_DIR}/core/udp.c
    ${LWIP_DIR}/core/timeouts.c
    ${LWIP_DIR}/core/ipv4/etharp.c
    ${LWIP_DIR}/core/ipv4/icmp.c
    ${LWIP_DIR}/core/ipv4/ip4.c
    ${LWIP_DIR}/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/netif/ethernet.c
    ${LWIP_DIR}/api/err.c
)

add_library(LwIP_Boot OBJECT ${LwIP_Boot_Src})
target_include_directories(LwIP_Boot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LwIP_Boot PUBLIC stm32cubemx)

# ---- Bootloader executable ----
add_executable(DP83848_BOOT)

target_include_directories(DP83848_BOOT PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}             # lwipopts.h (before LWIP/Target)
    ${CMAKE_SOURCE_DIR}/Core/Inc            # main.h, gpio.h, usart.h, boot_request.h
    ${CMAKE_SOURCE_DIR}/LWIP/App
    ${CMAKE_SOURCE_DIR}/LWIP/Target
)

target_sources(DP83848_BOOT PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bootloader_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tftp_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_if.c
    ${CMAKE_CURRENT_SOURCE_DIR}/iap_slot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lwip_boot.c
    ${CMAKE_SOURCE_DIR}/Core/Src/net_config.c
    ${CMAKE_SOURCE_DIR}/Core/Src/gpio.c
    ${CMAKE_SOURCE_DIR}/Core/Src/usart.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/LWIP/Target/ethernetif.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f407xx.s
)

# Bootloader must fit in 48KB (sectors 0-2). Use size optimization
# regardless of the global Debug/Release build type.
target_compile_options(DP83848_BOOT PRIVATE -Os)

# ---- Link ----
target_link_libraries(DP83848_BOOT PRIVATE
    STM32_Drivers      # shared HAL objects
    LwIP_Boot          # bootloader LwIP (UDP-only)
)

target_link_options(DP83848_BOOT PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F407XX_BOOT.ld
)

# Emit .bin for direct flashing
add_custom_command(TARGET DP83848_BOOT POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:DP83848_BOOT>
        $<TARGET_FILE_DIR:DP83848_BOOT>/DP83848_BOOT.bin
)
