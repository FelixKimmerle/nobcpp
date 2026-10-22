# A tiny CMake library for the dependency tests: a static library with a public
# define, an option and a transitive dependency (Threads), installed as a CMake package.
cmake_minimum_required(VERSION 3.16)
project(greetlib LANGUAGES CXX)

option(GREETLIB_LOUD "greet loudly" OFF)
find_package(Threads REQUIRED)

add_library(greetlib STATIC src/greetlib.cpp)
target_include_directories(greetlib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                           $<INSTALL_INTERFACE:include>)
target_compile_definitions(greetlib PUBLIC GREETLIB_IMPORTED=1)
if(GREETLIB_LOUD)
    target_compile_definitions(greetlib PRIVATE GREETLIB_LOUD)
endif()
target_link_libraries(greetlib PUBLIC Threads::Threads)

install(TARGETS greetlib EXPORT greetlibTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT greetlibTargets NAMESPACE greetlib:: DESTINATION lib/cmake/greetlib)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/greetlibConfig.cmake
     "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\ninclude(\${CMAKE_CURRENT_LIST_DIR}/greetlibTargets.cmake)\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/greetlibConfig.cmake DESTINATION lib/cmake/greetlib)
