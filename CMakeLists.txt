cmake_minimum_required(VERSION 3.8)
project(disruptor LANGUAGES CXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -Wall -Wextra -pedantic -Wno-unused-parameter")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS Off)

enable_testing()
add_subdirectory(test)
