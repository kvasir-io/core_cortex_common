# core_cortex_common

What every Cortex-M core layer shares, written once.

| Header               |                                                                          |
|----------------------|--------------------------------------------------------------------------|
| `Nvic.hpp`           | enable, disable, pend, unpend and priority of an interrupt               |
| `SystemControl.hpp`  | system reset (`SystemReset`, `resetNow()`: dsb, SYSRESETREQ, dsb, loop); SysTick pending and priority; the active exception's level |
| `Systick.hpp`        | `SystickClockBase<Config>`: `now()`, `delay()`, `delayUntil()`, `syncTo()`; its std::chrono shell is the SDK's `Kvasir::Chrono::SteadyClock` (`Util/SteadyClock.hpp`, the RP TIMER clock's too) |
| `SystickReading.hpp` | the register-free arithmetic of SysTick: one reading (`pairReading`), the overrun counter and its width (`OverrunWordT`, `calcOverRunValue`), the delay's stop rule and chunks (`delayReached`, `waitInChunks`) |
| `Barrier.hpp`        | `dmb dsb isb sev wfe wfi`                                                |
| `CoreInterrupts.hpp` | `CommonCoreInterrupts`, which a core's `CoreInterrupts` extends          |
| `InterruptRules.hpp` | the exception rules per Arm profile (`Core::CoreInterruptRules<P>`, `disabledWith`) |
| `Delay.hpp`          | `Core::spinCycles(n)` / `spinCycles<N>()`: at least n core cycles, a `subs; bne` loop (no nop: not a timing instruction, DDI0419E A6.7.47); asm only, RAM-function safe |
| `InterruptMask.hpp`  | PRIMASK: `Core::IrqGuard`, `IrqState` (msr + isb restore), `irqsMasked/maskIrqs/unmaskIrqs`; needs no register layer, no chip |
| `FaultSnapshot.hpp`  | `Fault::Core::Snapshot`: the fault registers of one fault, as each core's `snapshot()` read them |
| `FaultDecode.hpp`    | Armv7-M/Armv8-M: `Fault::Core::decode(Snapshot, FaultTable)`, pure and register-free; the core's table comes from its SVD |
| `FaultV7m.hpp`       | Armv7-M/Armv8-M: the shared fault body over the core's generated SCB - `V7m::faultTable<SCB_R>(lead rows)`, `snapshotOf<SCB_R, Secure>()`, `clearFaultStatus<SCB_R>()`; a core's `core/Fault.hpp` adds only its own rows (m33 STKOF), registers (m33 SAU), position asserts and fault enables |
| `FaultLog.hpp`       | Armv7-M/Armv8-M: the COREFAULT line (`Fault::Core::logCoreFault`); includes uc_log |
| `CleanCore.hpp`      | `Startup::Core::cleanCore()` for every core and its pieces (SysTick, ICSR, VTOR; FAULTMASK, CFSR/HFSR, FPCCR where the core has them) |
| `SecondaryCoreTraits.hpp` | `Startup::Core::SecondaryCoreTraits::cpacrEnable` for every core (0 on Armv6-M, CP10/CP11 full access otherwise) |
| `CoreAll.hpp`        | what every core's `core/core.hpp` is: the NVIC/SCB/SysTick blocks, the module headers, `core/Features.hpp`, `Debug.hpp` |
| `Debug.hpp`          | `Core::haltingDebug()` and, where the DWT has a cycle counter, `Core::Debug::{isDebuggerConnected, enableCycleCounter, cycleCount, keepCycleCounterRunning}`, chosen by `KVASIR_CORE_HAS_*`; one file for every core, no `core/Debug.hpp` |

No header here includes a chip header. A chip fact (`Nvic::InterruptOffsetTraits<Tag::User>`, from the chip's
`chip/Interrupt.hpp`) is read only when a firmware instantiates something: through a defaulted `Traits` parameter of a
function template. The core exceptions are checked against the
core's own `Core::InterruptRules` (each core's `core/CoreInterrupts.hpp`), from which a chip's traits derive and add
`end` and the device gaps.

## Use

A core (`core_cortex_m0plus`, `core_cortex_m33`, `core_cortex_m4`) has this repository as the submodule
`src/cortex_common`, and its `src/core/*.hpp` include `cortex_common/*.hpp`. Except `Debug.hpp` (included as
`cortex_common/Debug.hpp` wherever it is used), nothing above the core layer names it.

What differs between cores comes from the core's `core.svd`, whose priority fields cover only the
implemented bits. It has to provide:

- `NVIC`: `ISER/ICER/ISPR/ICPR` as `dim` arrays of `SETENA_%s/CLRENA_%s/SETPEND_%s/CLRPEND_%s`,
  `IPR` with `PRI_%s`
- `SCB`: `ICSR`, `AIRCR`, `SHPR2.PRI_11`, `SHPR3.PRI_15/PRI_14`
- `SYSTICK`: `CSR/RVR/CVR`
- Armv7-M/Armv8-M (the fault decode): `SCB.CFSR.UFSR/BFSR/MMFSR`, the registers `UFSR`, `BFSR`, `MMFSR` at CFSR's
  offset with their fields in CFSR bit positions, `HFSR.DEBUGEVT/FORCED/VECTTBL`

## Tests

    cmake -S tests -B build && cmake --build build && ctest --test-dir build
