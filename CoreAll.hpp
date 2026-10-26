#pragma once
// What every core's core/core.hpp is (core_cortex_m0plus, core_cortex_m33, core_cortex_m4 include this file and
// nothing else): the generated blocks every Cortex-M core has, the module headers, and the core's feature asserts.
// Every other generated core header (DCB, DWT, FP, SAU, ...) is included by the module that uses it (Debug.hpp,
// StartUp.hpp, Fault.hpp); include it yourself to use one. The `core/` headers are the core's own (CoreInterrupts,
// StartUp, Features) or its forwarders to this repository.
#include "core_peripherals/NVIC.hpp"
#include "core_peripherals/SCB.hpp"
#include "core_peripherals/SYSTICK.hpp"

//
#include "core/Barrier.hpp"
#include "core/CoreInterrupts.hpp"
#include "core/Features.hpp"
#include "core/Nvic.hpp"
#include "core/StartUp.hpp"
#include "core/SystemControl.hpp"
#include "core/Systick.hpp"
#include "cortex_common/Debug.hpp"
