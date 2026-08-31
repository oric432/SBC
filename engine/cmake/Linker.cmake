include(CheckCXXCompilerFlag)

# Prefers mold, then lld, over the platform default linker (GNU ld/BFD) for
# link speed -- measured ~7x faster for SbcEngine locally. Falls back
# silently when neither is installed so this never breaks a build that
# lacks them (e.g. a fresh WSL setup).
function(enable_fast_linker)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        return()
    endif()

    foreach(candidate mold lld)
        set(CMAKE_REQUIRED_FLAGS "-fuse-ld=${candidate}")
        check_cxx_compiler_flag("-fuse-ld=${candidate}" MYPROJECT_HAS_LD_${candidate})
        unset(CMAKE_REQUIRED_FLAGS)

        if(MYPROJECT_HAS_LD_${candidate})
            add_link_options("-fuse-ld=${candidate}")
            message(STATUS "Linker: using ${candidate} (faster than default)")
            return()
        endif()
    endforeach()

    message(STATUS "Linker: mold/lld not found, using platform default")
endfunction()
