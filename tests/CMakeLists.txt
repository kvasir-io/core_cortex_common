cmake_minimum_required(VERSION 3.28)

# Host tests of core_cortex_common.
#
# cmake -S tests -B tests/build -G Ninja && cmake --build tests/build && ctest --test-dir tests/build
#
# * systick_reading_test: SystickReading.hpp, this repository's headers only.
# * irq_state_syntax_<arch>: Special.hpp + InterruptMask.hpp (the PRIMASK API) compiled for Armv6-M and Armv8-M (clang
#   only).
# * interrupt_rules_test: InterruptRules.hpp (the exception rules per Arm profile) against the Arm tables, standard
#   headers only.
# * fault_decode_test: FaultDecode.hpp against ARM ARM reference tables, standard headers only.
# * decoders/: Nvic.hpp and SystemControl.hpp on the SDK's host register mock (KVASIR_REGISTER_MOCK), against the
#   core_peripherals headers svd_converter generates from each core's SVD - the NVIC action table, the active
#   exception's priority (Detail::activePriority), clearAllEnablesAndPending and the Startup decoders (InterruptOfAction
#   / PriorityOfAction), and on the v7-M/v8-M SCBs FaultV7m.hpp (decode table, snapshot read order, CFSR/HFSR clear).
#   One configuration per core SVD: Cortex-M0+ (2 priority bits), Cortex-M33 (4), Cortex-M4 (core.svd 3, core_prio4.svd
#   4). Needs Kvasir_SDK (src/, the tests/ support headers and the svd_converter submodule, which fetches its own
#   dependencies) and the three core repositories (their SVDs and src/core/CoreInterrupts.hpp). Each is the checkout
#   next to this one, else fetched; FETCHCONTENT_SOURCE_DIR_KVASIR_SDK / _CORE_CORTEX_M0PLUS / _CORE_CORTEX_M33 /
#   _CORE_CORTEX_M4 point a lone checkout at local copies.
project(cortex_common_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(systick_reading_test systick_reading_test.cpp)
target_include_directories(systick_reading_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_options(systick_reading_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(interrupt_rules_test interrupt_rules_test.cpp)
target_include_directories(interrupt_rules_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_options(interrupt_rules_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME interrupt_rules_test COMMAND interrupt_rules_test)

add_executable(fault_decode_test fault_decode_test.cpp)
target_include_directories(fault_decode_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_options(fault_decode_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME fault_decode_test COMMAND fault_decode_test)

# Special.hpp for the target, -fsyntax-only, Armv6-M and Armv8-M mainline: clang only (gcc would need one arm-none-eabi
# toolchain per job)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_arch "thumbv6m-none-eabi" "thumbv8m.main-none-eabihf")
        add_test(
            NAME irq_state_syntax_${_arch}
            COMMAND
                ${CMAKE_CXX_COMPILER} --target=${_arch} -std=c++23 -fsyntax-only -ffreestanding -nostdinc++ -isystem
                ${CMAKE_CURRENT_LIST_DIR}/target_std -Wall -Wextra -Werror -I${CMAKE_CURRENT_LIST_DIR}/..
                ${CMAKE_CURRENT_LIST_DIR}/irq_state_syntax.cpp)
    endforeach()
else()
    message(STATUS "irq_state_syntax: clang only, skipped with ${CMAKE_CXX_COMPILER_ID}")
endif()
add_test(NAME systick_reading_test COMMAND systick_reading_test)

get_filename_component(CORTEX_COMMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(_siblings "${CORTEX_COMMON_ROOT}/.." ABSOLUTE)

include(FetchContent)
# Kvasir_SDK: KVASIR_ROOT, else the checkout next to this repository, else fetched; its submodules and the other
# packages: kvasir_locate (Kvasir_SDK cmake/kvasir_locate.cmake)
if(NOT KVASIR_ROOT AND EXISTS "${CORTEX_COMMON_ROOT}/../Kvasir_SDK/cmake/kvasir_locate.cmake")
    get_filename_component(KVASIR_ROOT "${CORTEX_COMMON_ROOT}/../Kvasir_SDK" ABSOLUTE)
elseif(NOT KVASIR_ROOT)
    include(FetchContent)
    FetchContent_Declare(
        kvasir_sdk
        GIT_REPOSITORY https://github.com/kvasir-io/Kvasir_SDK.git
        GIT_TAG master
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "" SOURCE_SUBDIR fetch-only)
    FetchContent_MakeAvailable(kvasir_sdk)
    set(KVASIR_ROOT "${kvasir_sdk_SOURCE_DIR}")
endif()
include("${KVASIR_ROOT}/cmake/kvasir_locate.cmake")
kvasir_locate(SDK_SUBMODULES svd_converter)

foreach(_core core_cortex_m0plus core_cortex_m33 core_cortex_m4)
    string(TOUPPER ${_core} _var)
    if(NOT ${_var}_ROOT AND EXISTS ${_siblings}/${_core}/core.svd)
        set(${_var}_ROOT ${_siblings}/${_core})
    endif()
    if(NOT ${_var}_ROOT)
        # its SVDs and src/core only: its src/cortex_common submodule would be this checkout
        FetchContent_Declare(
            ${_core}
            GIT_REPOSITORY https://github.com/kvasir-io/${_core}.git
            GIT_TAG master
            GIT_SHALLOW TRUE
            GIT_SUBMODULES "" SOURCE_SUBDIR none)
        FetchContent_MakeAvailable(${_core})
        set(${_var}_ROOT ${${_core}_SOURCE_DIR})
    endif()
endforeach()

# svd_converter for the host: svd_convert() in decoders/
add_subdirectory(${KVASIR_ROOT}/svd_converter ${CMAKE_BINARY_DIR}/svd_converter EXCLUDE_FROM_ALL)

# One configuration per core SVD (decoders/CMakeLists.txt): its name, SVD, root, the implemented bits of a priority
# byte, whether it has SHPR1 (not on Armv6-M), the interrupt count of the decoder test's chip and the counts of the
# clearAllEnablesAndPending tests (one executable each: InterruptOffsetTraits is one specialisation per program; 33 and
# 65 need one word more than 32 and 64). The core.svd NVICs: Cortex-M0+ 1 ISER word (32 interrupts), M33 15, M4 8.
set(CORE_TEST_NAME m0plus)
set(CORE_TEST_PRIO_MASK 0xC0)
set(CORE_TEST_SHPR1 OFF)
set(CORE_TEST_SVD ${CORE_CORTEX_M0PLUS_ROOT}/core.svd)
set(CORE_TEST_ROOT ${CORE_CORTEX_M0PLUS_ROOT})
set(CORE_TEST_END 32)
set(CORE_TEST_CLEAR_ENDS 26 32)
add_subdirectory(decoders decoders_m0plus)

set(CORE_TEST_NAME m33)
set(CORE_TEST_PRIO_MASK 0xF0)
set(CORE_TEST_SHPR1 ON)
set(CORE_TEST_SVD ${CORE_CORTEX_M33_ROOT}/core.svd)
set(CORE_TEST_ROOT ${CORE_CORTEX_M33_ROOT})
set(CORE_TEST_END 52)
set(CORE_TEST_CLEAR_ENDS 26 32 33 52 102 136)
add_subdirectory(decoders decoders_m33)

set(CORE_TEST_NAME m4)
set(CORE_TEST_PRIO_MASK 0xE0)
set(CORE_TEST_SHPR1 ON)
set(CORE_TEST_SVD ${CORE_CORTEX_M4_ROOT}/core.svd)
set(CORE_TEST_ROOT ${CORE_CORTEX_M4_ROOT})
set(CORE_TEST_END 136)
set(CORE_TEST_CLEAR_ENDS 26 65 102 136)
add_subdirectory(decoders decoders_m4)

set(CORE_TEST_NAME m4_prio4)
set(CORE_TEST_PRIO_MASK 0xF0)
set(CORE_TEST_SHPR1 ON)
set(CORE_TEST_SVD ${CORE_CORTEX_M4_ROOT}/core_prio4.svd)
set(CORE_TEST_ROOT ${CORE_CORTEX_M4_ROOT})
set(CORE_TEST_END 102)
set(CORE_TEST_CLEAR_ENDS "")
add_subdirectory(decoders decoders_m4_prio4)
