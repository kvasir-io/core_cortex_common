# core_cortex_common

What every Cortex-M core layer shares, written once.

| Header               |                                                                          |
|----------------------|--------------------------------------------------------------------------|
| `Nvic.hpp`           | enable, disable, pend, unpend and priority of an interrupt               |
| `SystemControl.hpp`  | system reset; SysTick/PendSV/NMI pending; SysTick/PendSV/SVCall priority |
| `Systick.hpp`        | `SystickClockBase<Config>`: `now()`, `delay()`, `delayUntil()`, `syncTo()` |
| `SystickReading.hpp` | the register-free arithmetic of one SysTick reading                      |
| `Barrier.hpp`        | `dmb dsb isb sev wfe wfi`                                                |
| `CoreInterrupts.hpp` | `CommonCoreInterrupts`, which a core's `CoreInterrupts` extends          |

## Use

A core (`core_cortex_m0plus`, `core_cortex_m33`) has this repository as the submodule
`src/cortex_common`, and its `src/core/*.hpp` include `cortex_common/*.hpp`. Nothing above the
core layer names it.

What differs between cores comes from the core's `core.svd`, whose priority fields cover only the
implemented bits. It has to provide:

- `NVIC`: `ISER/ICER/ISPR/ICPR` as `dim` arrays of `SETENA_%s/CLRENA_%s/SETPEND_%s/CLRPEND_%s`,
  `IPR` with `PRI_%s`
- `SCB`: `ICSR`, `AIRCR`, `SHPR2.PRI_11`, `SHPR3.PRI_15/PRI_14`
- `SYSTICK`: `CSR/RVR/CVR`

## Tests

    cmake -S tests -B build && cmake --build build && ctest --test-dir build
