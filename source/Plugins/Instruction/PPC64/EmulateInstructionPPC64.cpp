//===-- EmulateInstructionPPC64.cpp ------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionPPC64.h"

#include <stdlib.h>

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"

#include "Plugins/Process/Utility/lldb-ppc64le-register-enums.h"

#define DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_ppc64le.h"

// debug messages
#define EMU_INST_PPC64_DEBUG 0
#if EMU_INST_PPC64_DEBUG
#define DPRINTF(...) printf(__VA_ARGS__)
#else
#define DPRINTF(...) do { ; } while (0)
#endif

using namespace lldb;
using namespace lldb_private;

void EmulateInstructionPPC64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionPPC64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString EmulateInstructionPPC64::GetPluginNameStatic() {
  ConstString g_plugin_name("lldb.emulate-instruction.ppc64");
  return g_plugin_name;
}

lldb_private::ConstString EmulateInstructionPPC64::GetPluginName() {
  static ConstString g_plugin_name("EmulateInstructionPPC64");
  return g_plugin_name;
}

const char *EmulateInstructionPPC64::GetPluginDescriptionStatic() {
  return "Emulate instructions for the PPC64 architecture.";
}

EmulateInstruction *
EmulateInstructionPPC64::CreateInstance(const ArchSpec &arch,
                                        InstructionType inst_type) {
  if (EmulateInstructionPPC64::SupportsEmulatingInstructionsOfTypeStatic(
          inst_type)) {
    if (arch.GetTriple().getArch() == llvm::Triple::ppc64 ||
        arch.GetTriple().getArch() == llvm::Triple::ppc64le) {
      return new EmulateInstructionPPC64(arch);
    }
  }

  return nullptr;
}

bool EmulateInstructionPPC64::SetTargetTriple(const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::ppc64)
    return true;
  else if (arch.GetTriple().getArch() == llvm::Triple::ppc64le)
    return true;

  return false;
}

static bool LLDBTableGetRegisterInfo(uint32_t reg_num, RegisterInfo &reg_info) {
  if (reg_num >= llvm::array_lengthof(g_register_infos_ppc64le))
    return false;
  reg_info = g_register_infos_ppc64le[reg_num];
  return true;
}

bool EmulateInstructionPPC64::GetRegisterInfo(RegisterKind reg_kind,
                                              uint32_t reg_num,
                                              RegisterInfo &reg_info) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_pc_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_r1_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_lr_ppc64le;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_kind = eRegisterKindLLDB;
      reg_num = gpr_cr_ppc64le;
      break;

    default:
      return false;
    }
  }

  if (reg_kind == eRegisterKindLLDB)
    return LLDBTableGetRegisterInfo(reg_num, reg_info);
  return false;
}

bool EmulateInstructionPPC64::ReadInstruction() {
  bool success = false;
  m_addr = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                                LLDB_INVALID_ADDRESS, &success);
  if (success) {
    Context ctx;
    ctx.type = eContextReadOpcode;
    ctx.SetNoArgs();
    m_opcode.SetOpcode32(
        ReadMemoryUnsigned(ctx, m_addr, 4, 0, &success),
        GetByteOrder());
  }
  if (!success)
    m_addr = LLDB_INVALID_ADDRESS;
  return success;
}

bool EmulateInstructionPPC64::CreateFunctionEntryUnwind(
    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindLLDB);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(gpr_r1_ppc64le, 0);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("EmulateInstructionPPC64");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetReturnAddressRegister(gpr_lr_ppc64le);
  return true;
}

EmulateInstructionPPC64::Opcode *
EmulateInstructionPPC64::GetOpcodeForInstruction(const uint32_t opcode) {
  static EmulateInstructionPPC64::Opcode g_opcodes[] = {
      {0xfc0007ff, 0x7c0002a6,
       &EmulateInstructionPPC64::EmulateMFSPR,
       "mfspr RT, SPR"},
      {0xfc000003, 0xf8000000,
       &EmulateInstructionPPC64::EmulateSTD,
       "std RS, DS(RA)"},
      {0xfc000003, 0xf8000001,
       &EmulateInstructionPPC64::EmulateSTD,
       "stdu RS, DS(RA)"},
      {0xfc0007fe, 0x7c000378,
       &EmulateInstructionPPC64::EmulateOR,
       "or RA, RS, RB"},
      {0xfc000000, 0x38000000,
       &EmulateInstructionPPC64::EmulateADDI,
       "addi RT, RA, SI"},
      {0xfc000003, 0xe8000000,
       &EmulateInstructionPPC64::EmulateLD,
       "ld RT, DS(RA)"}
  };
  static const size_t k_num_ppc_opcodes = llvm::array_lengthof(g_opcodes);

  for (size_t i = 0; i < k_num_ppc_opcodes; ++i) {
    if ((g_opcodes[i].mask & opcode) == g_opcodes[i].value)
      return &g_opcodes[i];
  }
  return nullptr;
}

bool EmulateInstructionPPC64::EvaluateInstruction(uint32_t evaluate_options) {
  const uint32_t opcode = m_opcode.GetOpcode32();
  // DPRINTF("PPC64::EvaluateInstruction: opcode=0x%08x\n", opcode);
  Opcode *opcode_data = GetOpcodeForInstruction(opcode);
  if (!opcode_data)
    return false;

  // DPRINTF("PPC64::EvaluateInstruction: %s\n", opcode_data->name);
  const bool auto_advance_pc =
      evaluate_options & eEmulateInstructionOptionAutoAdvancePC;

  bool success = false;

  uint32_t orig_pc_value = 0;
  if (auto_advance_pc) {
    orig_pc_value =
        ReadRegisterUnsigned(eRegisterKindLLDB, gpr_pc_ppc64le, 0, &success);
    if (!success)
      return false;
  }

  // Call the Emulate... function.
  success = (this->*opcode_data->callback)(opcode);
  if (!success)
    return false;

  if (auto_advance_pc) {
    uint32_t new_pc_value =
        ReadRegisterUnsigned(eRegisterKindLLDB, gpr_pc_ppc64le, 0, &success);
    if (!success)
      return false;

    if (auto_advance_pc && (new_pc_value == orig_pc_value)) {
      EmulateInstruction::Context context;
      context.type = eContextAdvancePC;
      context.SetNoArgs();
      if (!WriteRegisterUnsigned(context, eRegisterKindLLDB, gpr_pc_ppc64le,
                                 orig_pc_value + 4))
        return false;
    }
  }
  return true;
}

bool EmulateInstructionPPC64::EmulateMFSPR(const uint32_t opcode) {
  struct MFSPR {
    uint32_t last : 1;
    uint32_t op2 : 10;
    uint32_t spr : 10;
    uint32_t rt : 5;
    uint32_t op : 6;
  };

  enum {
    SPR_LR = 0x100
  };

  const MFSPR *inst = (const MFSPR *)&opcode;

  // For now, we're only insterested in 'mfspr r0, lr'
  if (inst->rt != gpr_r0_ppc64le || inst->spr != SPR_LR)
    return false;

  DPRINTF("EmulateMFSPR: 0x%08lX: mfspr r0, lr\n", m_addr);

  bool success;
  uint64_t lr =
      ReadRegisterUnsigned(eRegisterKindLLDB, gpr_lr_ppc64le, 0, &success);
  if (!success)
    return false;
  Context context;
  context.type = eContextWriteRegisterRandomBits;
  WriteRegisterUnsigned(context, eRegisterKindLLDB, gpr_r0_ppc64le, lr);
  DPRINTF("EmulateMFSPR: success!\n");
  return true;
}

bool EmulateInstructionPPC64::EmulateLD(const uint32_t opcode) {
  struct LD {
    uint32_t : 2;
    uint32_t ds : 14;
    uint32_t ra : 5;
    uint32_t rt : 5;
    uint32_t op : 6;
  };

  const LD *inst = (const LD *)&opcode;
  int32_t ds = llvm::SignExtend32<16>(inst->ds << 2);

  // For now, tracking only loads from 0(r1) to r1
  // (0(r1) is the ABI defined location to save previous SP)
  if (inst->ra != gpr_r1_ppc64le || inst->rt != gpr_r1_ppc64le || ds != 0)
    return false;

  DPRINTF("EmulateLD: 0x%08lX: ld r%d, %d(r%d)\n",
      m_addr, inst->rt, ds, inst->ra);

  RegisterInfo r1_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, gpr_r1_ppc64le, r1_info))
    return false;

  // restore SP
  Context ctx;
  ctx.type = eContextRestoreStackPointer;
  ctx.SetRegisterToRegisterPlusOffset(r1_info, r1_info, 0);

  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, gpr_r1_ppc64le, 0);
  DPRINTF("EmulateLD: success!\n");
  return true;
}

bool EmulateInstructionPPC64::EmulateSTD(const uint32_t opcode) {
  struct STD {
    uint32_t u : 2;
    uint32_t ds : 14;
    uint32_t ra : 5;
    uint32_t rs : 5;
    uint32_t op : 6;
  };

  const STD *inst = (const STD *)&opcode;

  // For now, tracking only stores to r1
  if (inst->ra != gpr_r1_ppc64le)
    return false;
  // ... and only stores of SP, FP and LR (moved into r0 by a previous mfspr)
  if (inst->rs != gpr_r1_ppc64le &&
      inst->rs != gpr_r31_ppc64le &&
      inst->rs != gpr_r30_ppc64le &&
      inst->rs != gpr_r0_ppc64le)
    return false;

  bool success;
  uint64_t rs =
      ReadRegisterUnsigned(eRegisterKindLLDB, inst->rs, 0, &success);
  if (!success)
    return false;

  int32_t ds = llvm::SignExtend32<16>(inst->ds << 2);
  DPRINTF("EmulateSTD: 0x%08lX: std%s r%d, %d(r%d)\n",
      m_addr, inst->u? "u" : "", inst->rs, ds, inst->ra);

  // Make sure that r0 is really holding LR value
  // (this won't catch unlikely cases, such as r0 being overwritten after mfspr)
  uint32_t rs_num = inst->rs;
  if (inst->rs == gpr_r0_ppc64le) {
    uint64_t lr =
      ReadRegisterUnsigned(eRegisterKindLLDB, gpr_lr_ppc64le, 0, &success);
    if (!success || lr != rs)
      return false;
    rs_num = gpr_lr_ppc64le;
  }

  // set context
  RegisterInfo rs_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, rs_num, rs_info))
    return false;
  RegisterInfo ra_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, inst->ra, ra_info))
    return false;

  Context ctx;
  ctx.type = eContextPushRegisterOnStack;
  ctx.SetRegisterToRegisterPlusOffset(rs_info, ra_info, ds);

  // store
  uint64_t ra =
      ReadRegisterUnsigned(eRegisterKindLLDB, inst->ra, 0, &success);
  if (!success)
    return false;

  lldb::addr_t addr = ra + ds;
  WriteMemory(ctx, addr, &rs, sizeof(rs));

  // update RA?
  if (inst->u) {
    Context ctx;
    // NOTE Currently, RA will always be equal to SP(r1)
    ctx.type = eContextAdjustStackPointer;
    WriteRegisterUnsigned(ctx, eRegisterKindLLDB, inst->ra, addr);
  }

  DPRINTF("EmulateSTD: success!\n");
  return true;
}

bool EmulateInstructionPPC64::EmulateOR(const uint32_t opcode) {
  struct OR {
    uint32_t rc : 1;
    uint32_t op2 : 10;
    uint32_t rb : 5;
    uint32_t ra : 5;
    uint32_t rs : 5;
    uint32_t op : 6;
  };

  const OR *inst = (const OR *)&opcode;
  // to be safe, process only the known 'mr r31/r30, r1' prologue instructions
  if (m_fp != LLDB_INVALID_REGNUM ||
      inst->rs != inst->rb ||
      (inst->ra != gpr_r30_ppc64le && inst->ra != gpr_r31_ppc64le) ||
      inst->rb != gpr_r1_ppc64le)
    return false;

  DPRINTF("EmulateOR: 0x%08lX: mr r%d, r%d\n", m_addr, inst->ra, inst->rb);

  // set context
  RegisterInfo ra_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, inst->ra, ra_info))
    return false;

  Context ctx;
  ctx.type = eContextSetFramePointer;
  ctx.SetRegister(ra_info);

  // move
  bool success;
  uint64_t rb =
      ReadRegisterUnsigned(eRegisterKindLLDB, inst->rb, 0, &success);
  if (!success)
    return false;
  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, inst->ra, rb);
  m_fp = inst->ra;
  DPRINTF("EmulateOR: success!\n");
  return true;
}

bool EmulateInstructionPPC64::EmulateADDI(const uint32_t opcode) {
  struct ADDI {
    uint32_t si : 16;
    uint32_t ra : 5;
    uint32_t rt : 5;
    uint32_t op : 6;
  };

  const ADDI *inst = (const ADDI *)&opcode;

  // handle stack adjustments only
  // (this is a typical epilogue operation, with ra == r1. If it's
  //  something else, then we won't know the correct value of ra)
  if (inst->rt != gpr_r1_ppc64le || inst->ra != gpr_r1_ppc64le)
    return false;

  int32_t si = llvm::SignExtend32<16>(inst->si);
  DPRINTF("EmulateADDI: 0x%08lX: addi r1, r1, %d\n", m_addr, si);

  // set context
  RegisterInfo r1_info;
  if (!GetRegisterInfo(eRegisterKindLLDB, gpr_r1_ppc64le, r1_info))
    return false;

  Context ctx;
  ctx.type = eContextRestoreStackPointer;
  ctx.SetRegisterToRegisterPlusOffset(r1_info, r1_info, 0);

  // adjust SP
  bool success;
  uint64_t r1 =
      ReadRegisterUnsigned(eRegisterKindLLDB, gpr_r1_ppc64le, 0, &success);
  if (!success)
    return false;
  WriteRegisterUnsigned(ctx, eRegisterKindLLDB, gpr_r1_ppc64le, r1 + si);
  DPRINTF("EmulateADDI: success!\n");
  return true;
}
