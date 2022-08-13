cmake_minimum_required(VERSION 3.11)

project(SlyCooper)

include_directories(ThirdParty)

add_subdirectory(ThirdParty/cubeb)
add_subdirectory(ThirdParty/zstd)
add_subdirectory(ThirdParty/glfw/glfw)
add_subdirectory(ThirdParty/fmt/fmt)
add_subdirectory(ThirdParty/googletest)

add_subdirectory(Sly2Decomp)