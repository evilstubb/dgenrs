cmake_minimum_required(VERSION 3.20)
project(dgenrs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

if (CMAKE_BINARY_DIR STREQUAL PROJECT_BINARY_DIR)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif ()

enable_testing()
add_subdirectory(src/util)
link_libraries(util)
add_subdirectory(test)
add_subdirectory(src/game)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        COMMAND ${CMAKE_COMMAND}
            -D INPUT=Doxyfile.in
            -D OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
            -D PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        MAIN_DEPENDENCY Doxyfile.in
        DEPENDS
            ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
            ${PROJECT_SOURCE_DIR}/.git/index
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_custom_target(
        doc
        COMMAND Doxygen::doxygen
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif ()
