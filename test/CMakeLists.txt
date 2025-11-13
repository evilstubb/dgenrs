find_package(GTest REQUIRED)
link_libraries(GTest::GTest GTest::Main)
add_executable(utest test-asset.cpp test-font.cpp test-image.cpp)
add_test(NAME main COMMAND utest)
