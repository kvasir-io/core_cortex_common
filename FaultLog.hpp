#pragma once
// The Armv7-M / Armv8-M COREFAULT line: one decoded fault (FaultDecode.hpp) and the stacked frame. In namespace
// Kvasir::Fault::Core on purpose: the log module comes from the scope ("fault.core"), so moving this function to
// another namespace moves the line to another module. The text (format, argument order, enumerator names) is what
// the READMEs' expected logs and the tools grep for - keep it as it is.
#include "FaultDecode.hpp"

#include <cstdint>
#include <uc_log/uc_log.hpp>

namespace Kvasir::Fault::Core {
// `frame` is the stacked exception frame (R0-R3, R12, LR, PC, xPSR), read here; the caller passes only a frame it
// checked.
inline void logCoreFault([[maybe_unused]] std::uint32_t const* frame,
                         [[maybe_unused]] std::uint32_t        excReturn,
                         [[maybe_unused]] FaultInfo const&     faultInfo) {
    UC_LOG_C(
      "COREFAULT type({}Fault) forced({}) info({}) flags({:#010x}) address({:#010x}) "
      "registers: PC={:#010x} R0={:#010x} R1={:#010x} R2={:#010x} R3={:#010x} R12={:#010x} "
      "LR={:#010x} "
      "xPSR={:#010x} EXC_RETURN={:#010x}",
      faultInfo.type,
      faultInfo.forced,
      faultInfo.description,
      faultInfo.statusBits,
      faultInfo.faultAddress,
      frame[6],
      frame[0],
      frame[1],
      frame[2],
      frame[3],
      frame[4],
      frame[5],
      frame[7],
      excReturn);
}
}   // namespace Kvasir::Fault::Core
