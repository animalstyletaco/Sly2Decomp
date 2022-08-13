include_directories(../ThirdParty)

add_executable(cooper
        Sly2Decomp.cpp )

target_link_libraries(cooper
        fmt
        cubeb
        zstd
        glfw
        imgui)