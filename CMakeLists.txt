cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "$ENV{HOME}/pico/pico-sdk")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(hid_pass C CXX ASM)

pico_sdk_init()

add_executable(hid_pass
    main.c
    # PIO-USB HCD/DCD implementation for TinyUSB
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

add_subdirectory(pio_usb)
add_subdirectory(cdc_stdio_lib)

target_link_libraries(hid_pass PRIVATE
    pico_stdlib
    pico_multicore
    hardware_clocks
    pico_pio_usb
    tinyusb_device
    tinyusb_host
    cdc_stdio_lib
)

target_include_directories(hid_pass PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Use TinyUSB implementation for PIO-USB
target_compile_definitions(hid_pass PRIVATE
    PIO_USB_USE_TINYUSB
    PICO_DEFAULT_UART_BAUD_RATE=115200
)

pico_enable_stdio_usb(hid_pass 0)
pico_enable_stdio_uart(hid_pass 1)

pico_add_extra_outputs(hid_pass)