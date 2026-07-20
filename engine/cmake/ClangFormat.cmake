    
macro(add_clang_format_target TARGET_NAME STARTING_DIR)
    file(GLOB_RECURSE CLANG_FORMAT_FILES
          "${STARTING_DIR}/*.cpp"
          "${STARTING_DIR}/*hpp")


    add_custom_target(${TARGET_NAME}
                      COMMAND clang-format -i ${CLANG_FORMAT_FILES}
                      WORKING_DIRECTORY  ${CMAKE_SOURCE_DIR}
                      COMMENT "Running clang-format on files")
endmacro()
