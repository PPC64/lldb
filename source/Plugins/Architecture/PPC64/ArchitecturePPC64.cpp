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
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
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

static int32_t GetLocalEntryOffset(Symbol *sym) {
  uint32_t flags = sym->GetFlags();
  unsigned char other = flags >> 8 & 0xFF;
  return llvm::ELF::decodePPC64LocalEntryOffset(other);
}

size_t ArchitecturePPC64::GetBytesToSkip(Thread &thread) const {
  TargetSP target = thread.CalculateTarget();

  // This code handles only ELF files
  if (target->GetArchitecture().GetTriple().getObjectFormat() !=
      llvm::Triple::ObjectFormatType::ELF)
    return 0;

  lldb::StackFrameSP curr_frame = thread.GetStackFrameAtIndex(0);
  SymbolContext sc = curr_frame->GetSymbolContext(eSymbolContextSymbol);

  if (!sc.symbol)
    return 0;

  addr_t curr_addr = thread.GetRegisterContext()->GetPC();
  addr_t lep =
      sc.symbol->GetLoadAddress(target.get()) + GetLocalEntryOffset(sc.symbol);
  if (curr_addr == lep)
    return sc.symbol->GetPrologueByteSize();

  return 0;
}

void ArchitecturePPC64::AdjustBreakpointAddress(Symbol *func,
                                                Address &addr) const {
  int32_t loffs = GetLocalEntryOffset(func);
  if (!loffs)
    return;

  addr.SetOffset(addr.GetOffset() + loffs);
}
