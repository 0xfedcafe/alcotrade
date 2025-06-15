cmake_minimum_required(VERSION 3.10)
project(alcotrade)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -gdwarf-4")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-4")

include("${CMAKE_SOURCE_DIR}/vendor/rapidjson.cmake")

find_package(Drogon REQUIRED)

file(GLOB SOURCES "src/*.cc")

add_executable(alcotrade main.cc ${SOURCES})

target_include_directories(alcotrade PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(alcotrade Drogon::Drogon rapidjson)
add_dependencies(alcotrade rapidjson)
