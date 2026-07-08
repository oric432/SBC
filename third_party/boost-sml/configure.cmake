add_library(boost_sml INTERFACE)
add_library(boost::sml ALIAS boost_sml)
add_library(third_party::boost_sml ALIAS boost_sml)
target_include_directories(boost_sml SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
