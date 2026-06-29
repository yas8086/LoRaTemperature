add_executable(tst_sample tst_sample.cpp)
target_link_libraries(tst_sample PRIVATE Qt6::Core Qt6::Test)
target_include_directories(tst_sample PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_sample COMMAND tst_sample)

add_executable(tst_appconfig tst_appconfig.cpp ${CMAKE_SOURCE_DIR}/src/AppConfig.cpp)
target_link_libraries(tst_appconfig PRIVATE Qt6::Core Qt6::Test)
target_include_directories(tst_appconfig PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_appconfig COMMAND tst_appconfig)

add_executable(tst_csvwriter tst_csvwriter.cpp ${CMAKE_SOURCE_DIR}/src/CsvWriter.cpp)
target_link_libraries(tst_csvwriter PRIVATE Qt6::Core Qt6::Test)
target_include_directories(tst_csvwriter PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_csvwriter COMMAND tst_csvwriter)
