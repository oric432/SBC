# third_party/Utils.cmake

# Macro to safely fetch and build a dependency while guaranteeing its test suites
# and examples are disabled, keeping our build fast and our test runner clean.
macro(sbc_build_dependency dep_name)
    set(BUILD_TESTING_OLD ${BUILD_TESTING})
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(${dep_name})
    
    set(BUILD_TESTING ${BUILD_TESTING_OLD} CACHE BOOL "" FORCE)
endmacro()

# Macro to expose a third-party target under the unified third_party:: namespace
# while automatically elevating its includes to SYSTEM includes to silence warnings.
function(sbc_expose_third_party ALIAS_NAME ORIGINAL_TARGET)
    # ALIAS_NAME should look like third_party::fmt
    string(REPLACE "third_party::" "" BASE_NAME "${ALIAS_NAME}")
    set(INTERNAL_WRAPPER "tp_${BASE_NAME}")

    add_library(${INTERNAL_WRAPPER} INTERFACE)
    add_library(${ALIAS_NAME} ALIAS ${INTERNAL_WRAPPER})

    if(TARGET ${ORIGINAL_TARGET})
        target_link_libraries(${INTERNAL_WRAPPER} INTERFACE ${ORIGINAL_TARGET})
        
        # Elevate includes to SYSTEM to silence compiler/clang-tidy warnings
        get_target_property(tgt_includes ${ORIGINAL_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(tgt_includes AND NOT tgt_includes STREQUAL "tgt_includes-NOTFOUND")
            set_property(TARGET ${INTERNAL_WRAPPER} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${tgt_includes}")
        endif()
    endif()
endfunction()
