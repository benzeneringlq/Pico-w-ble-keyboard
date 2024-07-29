set(PROJECT       demo)
set(PICO_BOARD    pico_w)
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

cmake_minimum_required(VERSION 3.12)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()
add_executable(${PROJECT}
    ${PROJECT}.c
    picow_bt_example_background.c
    picow_bt_example_common.c
)
target_include_directories(${PROJECT} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/config
)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Pico-PIO-USB pico_pio_usb_build)

# # print memory usage, enable all warnings
# target_link_options(${PROJECT} PRIVATE -Xlinker --print-memory-usage)
# target_compile_options(${PROJECT} PRIVATE -Wall -Wextra)

target_link_libraries(${PROJECT}
    pico_stdlib
    pico_btstack_ble
    pico_btstack_classic #not needed for BLE only demos
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_multicore
    pico_pio_usb
)
# enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

pico_add_extra_outputs(${PROJECT})

add_custom_command(TARGET ${PROJECT}
    POST_BUILD
    COMMAND picotool load  -x ${CMAKE_BINARY_DIR}/${PROJECT}.uf2)
