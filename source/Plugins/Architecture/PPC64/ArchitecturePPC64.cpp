//===-- ArchitecturePPC64.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/PPC64/ArchitecturePPC64.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"

#include "llvm/BinaryFormat/ELF.h"

using namespace lldb_private;
using namespace lldb;

ConstString ArchitecturePPC64::GetPluginNameStatic() {
  return ConstString("ppc64");
}

void ArchitecturePPC64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "PPC64-specific algorithms",
                                &ArchitecturePPC64::Create);
}

void ArchitecturePPC64::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitecturePPC64::Create);
}

std::unique_ptr<Architecture> ArchitecturePPC64::Create(const ArchSpec &arch) {
  if (arch.GetMachine() != llvm::Triple::ppc64 &&
      arch.GetMachine() != llvm::Triple::ppc64le)
    return nullptr;
  return std::unique_ptr<Architecture>(new ArchitecturePPC64());
}

ConstString ArchitecturePPC64::GetPluginName() { return GetPluginNameStatic(); }
uint32_t ArchitecturePPC64::GetPluginVersion() { return 1; }

size_t ArchitecturePPC64::GetBytesToSkip(Target &target, SymbolContext &sc,
                                         lldb::addr_t curr_addr,
                                         Address &func_start_address) const {
  // This code handles only ELF files
  if (target.GetArchitecture().GetTriple().getObjectFormat() !=
      llvm::Triple::ObjectFormatType::ELF) {
    return LLDB_INVALID_OFFSET;
  }

  auto GetLocalEntryOffset = [](Symbol &symbol) {
    uint32_t flags = symbol.GetFlags();
    unsigned char other = flags >> 8 & 0xFF;
    return llvm::ELF::decodePPC64LocalEntryOffset(other);
  };

  // For either functions or symbols, compare PC with both global and local
  // entry points, returning the number of bytes that should be skipped after
  // the function global entry point if any address matches.
  if (sc.function) {
    func_start_address = sc.function->GetAddressRange().GetBaseAddress();
    lldb::addr_t func_abs_start_addr =
        func_start_address.GetLoadAddress(&target);

    if (curr_addr == func_abs_start_addr) {
      return sc.function->GetPrologueByteSize();
    } else if (sc.symbol) {
      if (curr_addr == func_abs_start_addr + GetLocalEntryOffset(*sc.symbol))
        return sc.function->GetPrologueByteSize();
    }
  } else if (sc.symbol) {
    func_start_address = sc.symbol->GetAddress();
    lldb::addr_t func_abs_start_addr =
        func_start_address.GetLoadAddress(&target);
    if (curr_addr == func_abs_start_addr ||
        curr_addr == func_abs_start_addr + GetLocalEntryOffset(*sc.symbol))
      return sc.symbol->GetPrologueByteSize();
  }

  // If we reached this point, then we were not able to match the current
  // address with any function entry point.
  // As the platform-independent method will certainly be unable to do it too,
  // fall back to not skip anything.
  return 0;
}
