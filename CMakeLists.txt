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
set(CMAKE_PROJECT_NAME H7RTOS)

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

# Keep FreeRTOS heap ownership outside CubeMX-generated headers.
target_compile_definitions(stm32cubemx INTERFACE
    configAPPLICATION_ALLOCATED_HEAP=1
    configGENERATE_RUN_TIME_STATS=1
    configUSE_STATS_FORMATTING_FUNCTIONS=2
    ARM_MATH_CM7
)

# Add project-specific include paths that should survive CubeMX regeneration.
target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/DSP/Lib/GCC
)

set(USER_SOURCES
    src/bsp/imu963ra/imu963ra.c
    src/bsp/uart/uart_async.c
    src/bsp/ws2812b/driver_ws2812b.c
    src/bsp/ws2812b/driver_ws2812b_interface.c
    src/service/cli/shell_companion.c
    src/service/cli/shell.c
    src/service/cli/shell_ext.c
    src/service/cli/port/shell_port.c
    src/service/cli/log/log.c
    src/service/imu/imu_service.c
    src/service/imu/MadgwickAHRS/MadgwickAHRS.c
    src/service/matrix/matrix.c
    src/service/matrix/matrix_cmd.c
    src/service/tools/common_def.c
    src/service/tools/vec_math.c
    src/service/cli/cpu_cmd.c
    src/service/flip/flip_core.c
    src/app/app_water_sim.c
    src/app/app_water_sim_cmd.c
)
# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/rtos_heap.c
    ${USER_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_SOURCE_DIR}/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    configCHECK_FOR_STACK_OVERFLOW=2
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    arm_cortexM7lfsp_math
)
