//===-- ABISysV_ppc64le.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABISysV_ppc64le.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"

// Project includes
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/RegisterValue.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "Utility/PPC64LE_DWARF_Registers.h"

#define DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_ppc64le.h"
#undef DECLARE_REGISTER_INFOS_PPC64LE_STRUCT

using namespace lldb;
using namespace lldb_private;


static const uint32_t k_num_register_infos =
    llvm::array_lengthof(g_register_infos_ppc64le);

const lldb_private::RegisterInfo *
ABISysV_ppc64le::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos_ppc64le;
}

size_t ABISysV_ppc64le::GetRedZoneSize() const { return 224; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_ppc64le::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  static ABISP g_abi_sp;
  if (arch.GetTriple().getArch() == llvm::Triple::ppc64le) {
    if (!g_abi_sp)
      g_abi_sp.reset(new ABISysV_ppc64le(process_sp));
    return g_abi_sp;
  }
  return ABISP();
}

bool ABISysV_ppc64le::PrepareTrivialCall(Thread &thread, addr_t sp,
                                       addr_t func_addr, addr_t return_addr,
                                       llvm::ArrayRef<addr_t> args) const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    StreamString s;
    s.Printf("ABISysV_ppc64le::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%" PRIu64 " = 0x%" PRIx64, static_cast<uint64_t>(i + 1),
               args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const RegisterInfo *reg_info = nullptr;

  if (args.size() > 8) // TODO handle more than 8 arguments
    return false;

  for (size_t i = 0; i < args.size(); ++i) {
    reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                        LLDB_REGNUM_GENERIC_ARG1 + i);
    if (log)
      log->Printf("About to write arg%" PRIu64 " (0x%" PRIx64 ") into %s",
                  static_cast<uint64_t>(i + 1), args[i], reg_info->name);
    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;
  }

  // First, align the SP
  if (log)
    log->Printf("16-byte aligning SP: 0x%" PRIx64 " to 0x%" PRIx64,
                (uint64_t)sp, (uint64_t)(sp & ~0xfull));

  sp &= ~(0xfull); // 16-byte alignment
  sp -= 32;

  Status error;
  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  ProcessSP process_sp(thread.GetProcess());

  RegisterValue reg_value;

  if (log)
    log->Printf("Pushing the return address onto the stack: 0x%" PRIx64
                ": 0x%" PRIx64,
                (uint64_t)sp, (uint64_t)return_addr);

  // Save return address onto the stack
  if (!process_sp->WritePointerToMemory(sp+16, return_addr, error))
    return false;

  // Read the current SP value
  if (!reg_ctx->ReadRegister(sp_reg_info, reg_value))
    return false;

  //Save current SP onto the stack
  if (!process_sp->WritePointerToMemory(sp,
                                        (addr_t)(reg_value.GetBytes()),
                                        error))
    return false;

  // %r1 is set to the actual stack value.
  if (log)
    log->Printf("Writing SP: 0x%" PRIx64, (uint64_t)sp);

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  // %pc is set to the address of the called function.
  if (log)
    log->Printf("Writing IP: 0x%" PRIx64, (uint64_t)func_addr);

  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_info, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Thread &thread,
                                uint32_t *argument_register_ids,
                                unsigned int &current_argument_register,
                                addr_t &current_stack_argument) {
  if (bit_width > 64)
    return false; // Scalar can't hold large integer arguments

  if (current_argument_register < 6) {
    scalar = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
        argument_register_ids[current_argument_register], 0);
    current_argument_register++;
    if (is_signed)
      scalar.SignExtend(bit_width);
  } else {
    uint32_t byte_size = (bit_width + (8 - 1)) / 8;
    Status error;
    if (thread.GetProcess()->ReadScalarIntegerFromMemory(
            current_stack_argument, byte_size, is_signed, scalar, error)) {
      current_stack_argument += byte_size;
      return true;
    }
    return false;
  }
  return true;
}

bool ABISysV_ppc64le::GetArgumentValues(Thread &thread, ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  // Extract the register context so we can read arguments from registers

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  // Get the pointer to the first stack argument so we have a place to start
  // when reading data

  addr_t sp = reg_ctx->GetSP(0);

  if (!sp)
    return false;

  addr_t current_stack_argument = sp + 48; // jump over return address

  uint32_t argument_register_ids[8];

  argument_register_ids[0] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[1] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[2] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG3)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[3] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG4)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[4] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG5)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[5] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG6)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[6] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG7)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[7] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG8)
          ->kinds[eRegisterKindLLDB];

  unsigned int current_argument_register = 0;

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    // We currently only support extracting values with Clang QualTypes.
    // Do we care about others?
    CompilerType compiler_type = value->GetCompilerType();
    if (!compiler_type)
      return false;
    bool is_signed;

    if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
      ReadIntegerArgument(value->GetScalar(), compiler_type.GetBitSize(&thread),
                          is_signed, thread, argument_register_ids,
                          current_argument_register, current_stack_argument);
    } else if (compiler_type.IsPointerType()) {
      ReadIntegerArgument(value->GetScalar(), compiler_type.GetBitSize(&thread),
                          false, thread, argument_register_ids,
                          current_argument_register, current_stack_argument);
    }
  }

  return true;
}

Status ABISysV_ppc64le::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                           lldb::ValueObjectSP &new_value_sp) {
  Status error;
  if (!new_value_sp) {
    error.SetErrorString("Empty value object for return value.");
    return error;
  }

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type) {
    error.SetErrorString("Null clang type for return value.");
    return error;
  }

  Thread *thread = frame_sp->GetThread().get();

  bool is_signed;
  uint32_t count;
  bool is_complex;

  RegisterContext *reg_ctx = thread->GetRegisterContext().get();

  bool set_it_simple = false;
  if (compiler_type.IsIntegerOrEnumerationType(is_signed) ||
      compiler_type.IsPointerType()) {
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName("r3", 0);

    DataExtractor data;
    Status data_error;
    size_t num_bytes = new_value_sp->GetData(data, data_error);
    if (data_error.Fail()) {
      error.SetErrorStringWithFormat(
          "Couldn't convert return value to raw data: %s",
          data_error.AsCString());
      return error;
    }
    lldb::offset_t offset = 0;
    if (num_bytes <= 8) {
      uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);

      if (reg_ctx->WriteRegisterFromUnsigned(reg_info, raw_value))
        set_it_simple = true;
    } else {
      error.SetErrorString("We don't support returning longer than 64 bit "
                           "integer values at present.");
    }
  } else if (compiler_type.IsFloatingPointType(count, is_complex)) {
    if (is_complex)
      error.SetErrorString(
          "We don't support returning complex values at present");
    else {
      size_t bit_width = compiler_type.GetBitSize(frame_sp.get());
      if (bit_width <= 64) {
        DataExtractor data;
        Status data_error;
        size_t num_bytes = new_value_sp->GetData(data, data_error);
        if (data_error.Fail()) {
          error.SetErrorStringWithFormat(
              "Couldn't convert return value to raw data: %s",
              data_error.AsCString());
          return error;
        }

        unsigned char buffer[16];
        ByteOrder byte_order = data.GetByteOrder();

        data.CopyByteOrderedData(0, num_bytes, buffer, 16, byte_order);
        set_it_simple = true;
      } else {
        // FIXME - don't know how to do 80 bit long doubles yet.
        error.SetErrorString(
            "We don't support returning float values > 64 bits at present");
      }
    }
  }

  if (!set_it_simple) {
    // Okay we've got a structure or something that doesn't fit in a simple
    // register.
    // We should figure out where it really goes, but we don't support this yet.
    error.SetErrorString("We only support setting simple integer and float "
                         "return types at present.");
  }

  return error;
}

ValueObjectSP ABISysV_ppc64le::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  if (!return_compiler_type)
    return return_valobj_sp;

  // value.SetContext (Value::eContextTypeClangType, return_value_type);
  value.SetCompilerType(return_compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();
  if (type_flags & eTypeIsScalar) {
    value.SetValueType(Value::eValueTypeScalar);

    bool success = false;
    if (type_flags & eTypeIsInteger) {
      // Extract the register context so we can read arguments from registers
      const size_t byte_size = return_compiler_type.GetByteSize(nullptr);
      uint64_t raw_value = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
          reg_ctx->GetRegisterInfoByName("r3", 0), 0);
      const bool is_signed = (type_flags & eTypeIsSigned) != 0;
      switch (byte_size) {
      default:
        break;

      case sizeof(uint64_t):
        if (is_signed)
          value.GetScalar() = (int64_t)(raw_value);
        else
          value.GetScalar() = (uint64_t)(raw_value);
        success = true;
        break;

      case sizeof(uint32_t):
        if (is_signed)
          value.GetScalar() = (int32_t)(raw_value & UINT32_MAX);
        else
          value.GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
        success = true;
        break;

      case sizeof(uint16_t):
        if (is_signed)
          value.GetScalar() = (int16_t)(raw_value & UINT16_MAX);
        else
          value.GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
        success = true;
        break;

      case sizeof(uint8_t):
        if (is_signed)
          value.GetScalar() = (int8_t)(raw_value & UINT8_MAX);
        else
          value.GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
        success = true;
        break;
      }
    } else if (type_flags & eTypeIsFloat) {
      if (type_flags & eTypeIsComplex) {
        // Don't handle complex yet.
      } else {
        const size_t byte_size = return_compiler_type.GetByteSize(nullptr);
        if (byte_size <= sizeof(long double)) {
          const RegisterInfo *f1_info = reg_ctx->GetRegisterInfoByName("f1", 0);
          RegisterValue f1_value;
          if (reg_ctx->ReadRegister(f1_info, f1_value)) {
            DataExtractor data;
            if (f1_value.GetData(data)) {
              lldb::offset_t offset = 0;
              if (byte_size == sizeof(float)) {
                value.GetScalar() = (float)data.GetFloat(&offset);
                success = true;
              } else if (byte_size == sizeof(double)) {
                value.GetScalar() = (double)data.GetDouble(&offset);
                success = true;
              }
            }
          }
        }
      }
    }

    if (success)
      return_valobj_sp = ValueObjectConstResult::Create(
          thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsPointer) {
    unsigned r3_id =
        reg_ctx->GetRegisterInfoByName("r3", 0)->kinds[eRegisterKindLLDB];
    value.GetScalar() =
        (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(r3_id, 0);
    value.SetValueType(Value::eValueTypeScalar);
    return_valobj_sp = ValueObjectConstResult::Create(
        thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsVector) {
    const size_t byte_size = return_compiler_type.GetByteSize(nullptr);
    if (byte_size > 0) {
      const RegisterInfo *altivec_reg = reg_ctx->GetRegisterInfoByName("vr2", 0);
      if (altivec_reg) {
        if (byte_size <= altivec_reg->byte_size) {
          ProcessSP process_sp(thread.GetProcess());
          if (process_sp) {
            std::unique_ptr<DataBufferHeap> heap_data_ap(
                new DataBufferHeap(byte_size, 0));
            const ByteOrder byte_order = process_sp->GetByteOrder();
            RegisterValue reg_value;
            if (reg_ctx->ReadRegister(altivec_reg, reg_value)) {
              Status error;
              if (reg_value.GetAsMemoryData(
                      altivec_reg, heap_data_ap->GetBytes(),
                      heap_data_ap->GetByteSize(), byte_order, error)) {
                DataExtractor data(DataBufferSP(heap_data_ap.release()),
                                   byte_order, process_sp->GetTarget()
                                                   .GetArchitecture()
                                                   .GetAddressByteSize());
                return_valobj_sp = ValueObjectConstResult::Create(
                    &thread, return_compiler_type, ConstString(""), data);
              }
            }
          }
        }
      }
    }
  }

  return return_valobj_sp;
}

ValueObjectSP ABISysV_ppc64le::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  return GetReturnValueObjectSimple(thread, return_compiler_type);
}

bool ABISysV_ppc64le::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t lr_reg_num = ppc64le_dwarf::dwarf_lr_ppc64le;
  uint32_t sp_reg_num = ppc64le_dwarf::dwarf_r1_ppc64le;
  uint32_t pc_reg_num = ppc64le_dwarf::dwarf_pc_ppc64le;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  // The previous PC is in the LR
  row->SetRegisterLocationToRegister(pc_reg_num, lr_reg_num, true);
  unwind_plan.AppendRow(row);

  // All other registers are the same.

  unwind_plan.SetSourceName("ppc64le at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);

  return true;
}

bool ABISysV_ppc64le::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = ppc64le_dwarf::dwarf_r1_ppc64le;
  uint32_t pc_reg_num = ppc64le_dwarf::dwarf_lr_ppc64le;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int32_t ptr_size = 8;
  row->GetCFAValue().SetIsRegisterDereferenced(sp_reg_num);

  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * 2, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  row->SetRegisterLocationToAtCFAPlusOffset(ppc64le_dwarf::dwarf_cr_ppc64le, ptr_size, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("ppc64le default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetReturnAddressRegister(ppc64le_dwarf::dwarf_lr_ppc64le);
  return true;
}

bool ABISysV_ppc64le::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// See "Register Usage" in the
// "System V Application Binary Interface"
// "64-bit PowerPC ELF Application Binary Interface Supplement"
// current version is 1.9 released 2004 at
// http://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi-1.9.pdf

bool ABISysV_ppc64le::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (reg_info) {
    // Preserved registers are :
    //    r1,r2,r13-r31
    //    cr2-cr4 (partially preserved)
    //    f14-f31 (not yet)
    //    v20-v31 (not yet)
    //    vrsave (not yet)

    const char *name = reg_info->name;
    if (name[0] == 'r') {
      if ((name[1] == '1' || name[1] == '2') && name[2] == '\0')
        return true;
      if (name[1] == '1' && name[2] > '2')
        return true;
      if ((name[1] == '2' || name[1] == '3') && name[2] != '\0')
        return true;
    }

    if (name[0] == 'f' && name[1] >= '0' && name[2] <= '9') {
      if (name[2] == '\0')
        return false;
      if (name[1] == '1' && name[2] >= '4')
        return true;
      if ((name[1] == '2' || name[1] == '3') && name[2] != '\0')
        return true;
    }

    if (name[0] == 's' && name[1] == 'p' && name[2] == '\0') // sp
      return true;
    if (name[0] == 'f' && name[1] == 'p' && name[2] == '\0') // fp
      return false;
    if (name[0] == 'p' && name[1] == 'c' && name[2] == '\0') // pc
      return true;
  }
  return false;
}

void ABISysV_ppc64le::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for ppc64le targets", CreateInstance);
}

void ABISysV_ppc64le::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ABISysV_ppc64le::GetPluginNameStatic() {
  static ConstString g_name("sysv-ppc64le");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

lldb_private::ConstString ABISysV_ppc64le::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ABISysV_ppc64le::GetPluginVersion() { return 1; }
