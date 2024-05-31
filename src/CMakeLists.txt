cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(test_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(app
	main.c
)

pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)

pico_add_extra_outputs(app)

target_include_directories(app PRIVATE
	.
)

pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_link_libraries(app
	pico_stdlib
	pico_stdio
	pico_time
	hardware_pio
	hardware_timer

	pico_stdlib
	pico_multicore
	hardware_adc
	hardware_uart
	hardware_dma
	hardware_pwm
)
