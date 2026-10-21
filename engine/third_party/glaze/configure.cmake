sbc_build_dependency(glaze)

target_compile_definitions(glaze_glaze INTERFACE GLZ_USE_BOOST_ASIO)

sbc_expose_third_party(third_party::glaze glaze::glaze)
