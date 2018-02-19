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
  /// prologue.
  ///
  /// This is specifically used for PPC64, where functions may have
  /// more than one entry point, so both should be compared with
  /// the current value of PC, in order to find out the number of
  /// bytes that should be skipped, in case we are stopped at a
  /// function entry point.
  ///
  /// Note that the value returned by this method is added to the
  /// address of the first function instruction, which
  /// corresponds to its entry point for other architectures, and to
  /// PPC64's global entry point. For PPC64, the local entry point
  /// is always an offset from the global entry point, corresponding
  /// to instructions that should be skipped if the function is being
  /// called from a local context. Thus, this method should always
  /// return the same value, regardless of the PC being at a global
  /// or local entry point.
  ///
  /// Returning an invalid offset means this method was not able to
  /// get the number of bytes to skip, and that the caller should
  /// use the standard platform-independent method to get it.
  //------------------------------------------------------------------
  virtual size_t GetBytesToSkip(ThreadPlan &thread_plan,
                                StackFrame &curr_frame) const {
    return LLDB_INVALID_OFFSET;
  }

private:
  Architecture(const Architecture &) = delete;
  void operator=(const Architecture &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_ARCHITECTURE_H
