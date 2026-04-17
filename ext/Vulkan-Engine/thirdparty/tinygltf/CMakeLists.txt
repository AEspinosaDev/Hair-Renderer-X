cmake_minimum_required(VERSION 3.16)
project(tinygltf)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
