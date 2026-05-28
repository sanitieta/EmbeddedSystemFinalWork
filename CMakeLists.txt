cmake_minimum_required(VERSION 3.20)
project(main C)

# ARM Compiler 5 built-in headers would normally come from Keil's toolchain.
# This simulates them so CLion can resolve <stdint.h> etc. when armcc is not installed.
add_compile_definitions(
    __CC_ARM
    TM4C1294NCPDT
    PART_TM4C1294NCPDT
    _RTE_
)

include_directories(
    "${CMAKE_SOURCE_DIR}/inc"
    "${CMAKE_SOURCE_DIR}/driverlib"
    "${CMAKE_SOURCE_DIR}/RTE/_Target_1"
)

add_executable(${PROJECT_NAME}
    main.c
    RTE/Device/TM4C1294NCPDT/system_TM4C129.c
    RTE/Device/TM4C1294NCPDT/startup_TM4C129.s
)
