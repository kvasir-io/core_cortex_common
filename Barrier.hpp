#pragma once

// Barrier and event instructions. Each clobbers "memory" so the compiler cannot move a load or
// store across it.
namespace Kvasir::Core {

[[gnu::always_inline]] inline void dmb() { asm volatile("dmb" ::: "memory"); }

[[gnu::always_inline]] inline void dsb() { asm volatile("dsb" ::: "memory"); }

[[gnu::always_inline]] inline void isb() { asm volatile("isb" ::: "memory"); }

// Wakes every core sleeping in wfe().
[[gnu::always_inline]] inline void sev() { asm volatile("sev" ::: "memory"); }

// On Armv8-M a successful exclusive store on this core sets the event register (DDI0553
// B9.3.1), so wfe() right after a std::atomic RMW, Spinlock or CriticalSection does not sleep:
// check with a plain load or register read, and re-check after waking.
[[gnu::always_inline]] inline void wfe() { asm volatile("wfe" ::: "memory"); }

[[gnu::always_inline]] inline void wfi() { asm volatile("wfi" ::: "memory"); }

}   // namespace Kvasir::Core
