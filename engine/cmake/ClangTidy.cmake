# ==============================================================================
# ClangTidy.cmake
# Standalone, reusable CMake module for integrating clang-tidy into the build graph
# ==============================================================================

function(enable_clang_tidy_for_target TARGET ENABLE_CLANG_TIDY)
    if(ENABLE_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES clang-tidy)
        if(CLANG_TIDY_EXE)
            set_target_properties(${TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
            # We don't message STATUS here to avoid spamming for every target
        else()
            if(NOT CLANG_TIDY_WARNING_EMITTED)
                message(WARNING "Clang-tidy requested, but clang-tidy binary was not found in PATH")
                set(CLANG_TIDY_WARNING_EMITTED TRUE CACHE INTERNAL "Clang tidy warning emitted")
            endif()
        endif()
    endif()
endfunction()