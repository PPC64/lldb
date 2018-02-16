//===-- EmulateInstructionPPC64.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef EmulateInstructionPPC64_h_
#define EmulateInstructionPPC64_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class EmulateInstructionPPC64 : public lldb_private::EmulateInstruction {
public:
  EmulateInstructionPPC64(const lldb_private::ArchSpec &arch)
      : EmulateInstruction(arch) {}

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::EmulateInstruction *
  CreateInstance(const lldb_private::ArchSpec &arch,
                 lldb_private::InstructionType inst_type);

  static bool SupportsEmulatingInstructionsOfTypeStatic(
      lldb_private::InstructionType inst_type) {
    switch (inst_type) {
    case lldb_private::eInstructionTypeAny:
    case lldb_private::eInstructionTypePrologueEpilogue:
      return true;

    case lldb_private::eInstructionTypePCModifying:
    case lldb_private::eInstructionTypeAll:
      return false;
    }
    return false;
  }

  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override { return 1; }

  bool SetTargetTriple(const lldb_private::ArchSpec &arch) override;

  bool SupportsEmulatingInstructionsOfType(
      lldb_private::InstructionType inst_type) override {
    return SupportsEmulatingInstructionsOfTypeStatic(inst_type);
  }

  bool ReadInstruction() override;

  bool EvaluateInstruction(uint32_t evaluate_options) override;

  bool TestEmulation(lldb_private::Stream *out_stream,
                     lldb_private::ArchSpec &arch,
                     lldb_private::OptionValueDictionary *test_data) override {
    return false;
  }

  bool GetRegisterInfo(lldb::RegisterKind reg_kind, uint32_t reg_num,
                       lldb_private::RegisterInfo &reg_info) override;

  bool
  CreateFunctionEntryUnwind(lldb_private::UnwindPlan &unwind_plan) override;

private:
  struct Opcode {
    uint32_t mask;
    uint32_t value;
    bool (EmulateInstructionPPC64::*callback)(uint32_t opcode);
    const char *name;
  };

  uint32_t m_fp = LLDB_INVALID_REGNUM;

  Opcode *GetOpcodeForInstruction(uint32_t opcode);

  bool EmulateMFSPR(uint32_t opcode);
  bool EmulateLD(uint32_t opcode);
  bool EmulateSTD(uint32_t opcode);
  bool EmulateOR(uint32_t opcode);
  bool EmulateADDI(uint32_t opcode);
};

} // namespace lldb_private

#endif // EmulateInstructionPPC64_h_
