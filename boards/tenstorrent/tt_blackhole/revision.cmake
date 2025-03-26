if (BOARD_QUALIFIERS STREQUAL "/tt_blackhole/bmc")
    set(BOARD_REVISIONS "p100" "p100a" "p150a")
    if(NOT DEFINED BOARD_REVISION)
    set(BOARD_REVISION "p100")
    else()
    if(NOT BOARD_REVISION IN_LIST BOARD_REVISIONS)
        message(FATAL_ERROR "${BOARD_REVISION} is not a valid revision for tt_blackhole. Accepted revisions: ${BOARD_REVISIONS}")
    endif()
    endif()

    # It appears that zephyr doesn't generate a preprocessor definition for board revision that can be ifdef'd...
    # So we just make it here
    string(TOUPPER ${BOARD_REVISION} BOARD_REVISION_UPPER)
    set(BOARD_REVISION_UPPER "CONFIG_BOARD_REVISION_${BOARD_REVISION_UPPER}")
    add_compile_definitions(${BOARD_REVISION_UPPER}=1)
    unset(BOARD_REVISION_UPPER)
endif()
