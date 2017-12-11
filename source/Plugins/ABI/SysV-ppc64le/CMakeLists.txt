add_lldb_library(lldbPluginABISysV_ppc64le PLUGIN
  ABISysV_ppc64le.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
  )
