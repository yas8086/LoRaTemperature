add_executable(tst_sample tst_sample.cpp)
target_link_libraries(tst_sample PRIVATE Qt6::Core Qt6::Test)
target_include_directories(tst_sample PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_sample COMMAND tst_sample)
