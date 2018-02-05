//===-- Architecture.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_ARCHITECTURE_H
#define LLDB_CORE_ARCHITECTURE_H

#include "lldb/Core/PluginInterface.h"

namespace lldb_private {

class Architecture : public PluginInterface {
public:
  Architecture() = default;
  virtual ~Architecture() = default;

  //------------------------------------------------------------------
  /// This is currently intended to handle cases where a
  /// program stops at an instruction that won't get executed and it
  /// allows the stop reason, like "breakpoint hit", to be replaced
  /// with a different stop reason like "no stop reason".
  ///
  /// This is specifically used for ARM in Thumb code when we stop in
  /// an IT instruction (if/then/else) where the instruction won't get
  /// executed and therefore it wouldn't be correct to show the program
  /// stopped at the current PC. The code is generic and applies to all
  /// ARM CPUs.
  //------------------------------------------------------------------
  virtual void OverrideStopInfo(Thread &thread) = 0;

  //------------------------------------------------------------------
  /// This method is used to get the number of bytes that should be
  /// skipped to reach the first instruction after a function
  /// prologue. This number is always relative to first function
  /// instruction, which should be set at 'func_start_address',
  /// if this method returns any value other than LLDB_INVALID_OFFSET.
  /// The first function instruction is defined here as the
  /// instruction found at the address of the function symbol and
  /// thus is independent of which entry point was used to enter the
  /// function.
  ///
  /// Returning LLDB_INVALID_OFFSET means this method was not able to
  /// get the number of bytes to skip, and that the caller should
  /// use the standard platform-independent method to get it.
  ///
  /// This is specifically used for PPC64, where functions may have
  /// more than one entry point, global and local, so both should
  /// be compared with curr_addr, in order to find out the number of
  /// bytes that should be skipped, in case we are stopped at either
  /// function entry point.
  //------------------------------------------------------------------
  virtual size_t GetBytesToSkip(Target &target, SymbolContext &sc,
                                lldb::addr_t curr_addr,
                                Address &func_start_address) const {
    return LLDB_INVALID_OFFSET;
  }

private:
  Architecture(const Architecture &) = delete;
  void operator=(const Architecture &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_ARCHITECTURE_H
