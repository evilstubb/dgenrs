find_package(Git REQUIRED)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    COMMAND ${CMAKE_COMMAND}
        -D INPUT=version.cpp.in
        -D OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
    MAIN_DEPENDENCY version.cpp.in
    DEPENDS
        ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        ${PROJECT_SOURCE_DIR}/.git/index
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Freetype REQUIRED)
find_package(harfbuzz REQUIRED)
find_package(PNG REQUIRED)
find_package(SDL3 REQUIRED)
link_libraries(Freetype::Freetype harfbuzz::harfbuzz PNG::PNG SDL3::SDL3)

add_library(
    util OBJECT

    asset.cpp asset.hpp
    font.cpp font.hpp
    image.cpp image.hpp
    util.cpp util.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp version.hpp
)
target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
