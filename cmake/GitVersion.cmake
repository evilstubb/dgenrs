foreach (VAR INPUT OUTPUT GIT_EXECUTABLE)
    if (NOT ${VAR})
        message(FATAL_ERROR "Required variable is missing: ${VAR}")
    endif ()
endforeach ()

execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
configure_file(${INPUT} ${OUTPUT} @ONLY)
