cmake_minimum_required(VERSION 3.16)
project(ETC-IDE VERSION 1.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_executable(come
  compiler/main.cpp
  compiler/lexer.cpp
  compiler/parser.cpp
  compiler/semantic.cpp
  compiler/codegen.cpp
)
target_compile_options(come PRIVATE -O3 -Wall)
if(MINGW)
  target_link_options(come PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(come PRIVATE pthread)
endif()
