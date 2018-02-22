if ("AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
  list(APPEND INST_EMU_SRCS "TestArm64InstEmulation.cpp")
  list(APPEND INST_EMU_LIBS "lldbPluginInstructionARM64")
endif()

if ("PowerPC" IN_LIST LLVM_TARGETS_TO_BUILD)
  list(APPEND INST_EMU_SRCS "TestPPC64InstEmulation.cpp")
  list(APPEND INST_EMU_LIBS "lldbPluginInstructionPPC64")
endif()

if (INST_EMU_SRCS)
  add_lldb_unittest(InstEmulationTests
    ${INST_EMU_SRCS}
    LINK_LIBS
      lldbCore
      lldbSymbol
      lldbTarget
      lldbPluginUnwindAssemblyInstEmulation
      lldbPluginDisassemblerLLVM
      ${INST_EMU_LIBS}
      lldbPluginProcessUtility
    LINK_COMPONENTS
      Support
      ${LLVM_TARGETS_TO_BUILD})
endif()
