if(CONFIG_MCUBOOT)
  # Append source file with mcuboot hooks
  target_sources(app PRIVATE mcuboot_hooks.c)
endif()
