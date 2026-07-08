add_executable(schema_generator schema_generator.cpp)

target_include_directories(schema_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(schema_generator PRIVATE
    project_options
    project_warnings
    third_party::glaze
)
