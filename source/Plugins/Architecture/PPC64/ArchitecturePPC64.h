//===-- ArchitecturePPC64.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGIN_ARCHITECTURE_PPC64_H
#define LLDB_PLUGIN_ARCHITECTURE_PPC64_H

#include "lldb/Core/Architecture.h"

namespace lldb_private {

class ArchitecturePPC64 : public Architecture {
public:
  static ConstString GetPluginNameStatic();
  static void Initialize();
  static void Terminate();

  ConstString GetPluginName() override;
  uint32_t GetPluginVersion() override;

  void OverrideStopInfo(Thread &thread) override {}

  //------------------------------------------------------------------
  /// On PPC64, the global entry point corresponds to first function
  /// instruction and the local entry point is always an offset from
  /// the global entry point, corresponding to the instructions that
  /// should be skipped if the function is being called from a local
  /// context.
  /// This method compares 'curr_addr' with both global and local
  /// function entry points and if either one is equal to 'curr_addr'
  /// then 'func_start_address' is set to the global entry point and
  /// the prologue size in bytes (relative to the global entry point)
  /// is returned.
  //------------------------------------------------------------------
  virtual size_t GetBytesToSkip(Target &target, SymbolContext &sc,
                                lldb::addr_t curr_addr,
                                Address &func_start_address) const override;

private:
  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitecturePPC64() = default;
};

} // namespace lldb_private

#endif // LLDB_PLUGIN_ARCHITECTURE_PPC64_H
