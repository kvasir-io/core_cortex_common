# One core SVD's decoder tests (added once per configuration by ../CMakeLists.txt): CORE_TEST_NAME, CORE_TEST_SVD,
# CORE_TEST_ROOT, CORE_TEST_PRIO_MASK, CORE_TEST_SHPR1, CORE_TEST_END, CORE_TEST_CLEAR_ENDS.

# core_peripherals/NVIC.hpp, SCB.hpp, ... of this SVD, converted as the core packages convert it (svd_convert's
# defaults, Kvasir_SDK cmake/chip_package.cmake kvasir_chip_svd)
svd_convert(core_peripherals_${CORE_TEST_NAME} SVD_FILE ${CORE_TEST_SVD} OUTPUT_DIRECTORY core_peripherals)

# "core/CoreInterrupts.hpp" from the core, "cortex_common/..." from this checkout (the core's own src/cortex_common is a
# submodule pointing at another commit of it)
set(_include ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${_include}/core)
file(CREATE_LINK ${CORE_TEST_ROOT}/src/core/CoreInterrupts.hpp ${_include}/core/CoreInterrupts.hpp SYMBOLIC)
# core/InterruptMask.hpp, core/Special.hpp and core/Barrier.hpp are forwarders to this checkout's headers in every core;
# a quoted include in a linked header ("Barrier.hpp" in InterruptMask.hpp) is found beside the link
file(CREATE_LINK ${CORTEX_COMMON_ROOT}/InterruptMask.hpp ${_include}/core/InterruptMask.hpp SYMBOLIC)
file(CREATE_LINK ${CORTEX_COMMON_ROOT}/Special.hpp ${_include}/core/Special.hpp SYMBOLIC)
file(CREATE_LINK ${CORTEX_COMMON_ROOT}/Barrier.hpp ${_include}/core/Barrier.hpp SYMBOLIC)
file(CREATE_LINK ${CORTEX_COMMON_ROOT} ${_include}/cortex_common SYMBOLIC)

set(_generated ${CMAKE_CURRENT_BINARY_DIR}/svd_converter_generated)
set(_defines KVASIR_REGISTER_MOCK KVASIR_TEST_PRIO_MASK=${CORE_TEST_PRIO_MASK})
if(CORE_TEST_SHPR1)
    list(APPEND _defines KVASIR_TEST_HAS_SHPR1)
endif()
set(_dirs ${_include} ${CMAKE_CURRENT_LIST_DIR} ${_generated} ${KVASIR_ROOT}/src ${KVASIR_ROOT}/tests)

function(_core_test name source end)
    set(_target ${name}_${CORE_TEST_NAME})
    add_executable(${_target} ${source})
    add_dependencies(${_target} core_peripherals_${CORE_TEST_NAME})
    target_include_directories(${_target} PRIVATE ${_dirs})
    target_compile_definitions(${_target} PRIVATE ${_defines} KVASIR_TEST_IRQ_END=${end})
    target_compile_features(${_target} PRIVATE cxx_std_26)
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ${_target} COMMAND ${_target})
endfunction()

_core_test(nvic_decoders decoders_test.cpp ${CORE_TEST_END})
# Kvasir_SDK StartUp/RamVectorTable.hpp over the generated SCB (its only host test)
_core_test(ram_vector_table ram_vector_table_test.cpp ${CORE_TEST_END})
foreach(_end IN LISTS CORE_TEST_CLEAR_ENDS)
    _core_test(clear_all_${_end} clear_all_test.cpp ${_end})
endforeach()

# core_cortex_m33's core/Mpu.hpp over its core.svd's MPU block (the other cores have no MPU description)
if(CORE_TEST_NAME STREQUAL "m33")
    file(CREATE_LINK ${CORE_TEST_ROOT}/src/core/Mpu.hpp ${_include}/core/Mpu.hpp SYMBOLIC)
    _core_test(mpu mpu_test.cpp ${CORE_TEST_END})
endif()

# FaultV7m.hpp (the shared v7-M/v8-M fault body) over the Armv7-M and Armv8-M SCBs
if(NOT CORE_TEST_NAME STREQUAL "m0plus")
    _core_test(fault_v7m fault_v7m_test.cpp ${CORE_TEST_END})
endif()

# The refusals: must_fail.cpp with -DMUST_FAIL=n, the compiler's message checked
set(_flags -std=c++26 -fsyntax-only -DKVASIR_TEST_IRQ_END=${CORE_TEST_END} -DKVASIR_TEST_IRQ_GAP=3)
foreach(_define IN LISTS _defines)
    list(APPEND _flags -D${_define})
endforeach()
foreach(_dir IN LISTS _dirs)
    list(APPEND _flags -I${_dir})
endforeach()
# the level (with the field's highest), the table's end, SysTick's level, a gap (the stand-in chip's line 3)
set(_must_fail_messages
    "interrupt priority out of range for this core: [0-9]+ is not in 0\\.\\.[0-9]+"
    "interrupt index outside this chip's vector table"
    "interrupt priority out of range for this core: [0-9]+ is not in 0\\.\\.[0-9]+"
    "interrupt index is a gap of this chip's vector table")
set(_n 0)
foreach(_message IN LISTS _must_fail_messages)
    math(EXPR _n "${_n} + 1")
    add_test(NAME nvic_must_fail_${_n}_${CORE_TEST_NAME} COMMAND ${CMAKE_CXX_COMPILER} ${_flags} -DMUST_FAIL=${_n}
                                                                 ${CMAKE_CURRENT_LIST_DIR}/must_fail.cpp)
    set_tests_properties(nvic_must_fail_${_n}_${CORE_TEST_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "${_message}")
endforeach()
