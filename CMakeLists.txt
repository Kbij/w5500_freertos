# CMake minimum required version
cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/libraries/pico-sdk)
    message(STATUS "PICO_SDK_PATH = ${PICO_SDK_PATH}")
endif()

include(rp2040_hat_freertos_c-patch.cmake)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(rp2040_hat_freertos_c_sdk_version.cmake)

# Set project name
set(PROJECT_NAME W5500FreeRtos)

# Set project informations
project(${PROJECT_NAME} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_BOARD "pico")

# Initialize the SDK
pico_sdk_init()

# Set ethernet chip
set(WIZNET_CHIP W5500)

if(${WIZNET_CHIP} STREQUAL W5100S)
    add_definitions(-D_WIZCHIP_=W5100S)
elseif(${WIZNET_CHIP} STREQUAL W5500)
    add_definitions(-D_WIZCHIP_=W5500)
else()
    message(FATAL_ERROR "WIZNET_CHIP is wrong = ${WIZNET_CHIP}")
endif()

message(STATUS "WIZNET_CHIP = ${WIZNET_CHIP}")

# Set the project root directory if it's not already defined, as may happen if
# the tests folder is included directly by a parent project, without including
# the top level CMakeLists.txt.
if(NOT DEFINED FREERTOS_DIR)
    set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/libraries/FreeRTOS-Kernel)
    message(STATUS "FREERTOS_DIR = ${FREERTOS_DIR}")
endif()

if(NOT DEFINED WIZNET_DIR)
    set(WIZNET_DIR ${CMAKE_SOURCE_DIR}/libraries/ioLibrary_Driver)
    message(STATUS "WIZNET_DIR = ${WIZNET_DIR}")
endif()

if(NOT DEFINED PORT_DIR)
    set(PORT_DIR ${CMAKE_SOURCE_DIR}/port)
    message(STATUS "PORT_DIR = ${PORT_DIR}")
endif()


# Add libraries in subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/libraries)
#add_subdirectory(${FREERTOS_DIR})

add_subdirectory(${PORT_DIR})

# Set compile options
add_compile_options(
        -Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

add_executable(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/src/main.c ${CMAKE_SOURCE_DIR}/src/server.c)
# target_include_directories(${PROJECT_NAME} PRIVATE
#     ${CMAKE_CURRENT_LIST_DIR}
# )

pico_add_extra_outputs(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE
        pico_stdlib
        hardware_spi
        hardware_dma
        FREERTOS_FILES
        ETHERNET_FILES
        IOLIBRARY_FILES
        DHCP_FILES
        DNS_FILES
        TIMER_FILES
        )

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)