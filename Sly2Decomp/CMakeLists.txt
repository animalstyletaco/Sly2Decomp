include_directories(../ThirdParty)

add_library(CooperGame
            game/opengl.cpp
            game/opengl_utils.cpp
            game/OpenGlRenderer.cpp
            game/Graphics.cpp
            game/Display.cpp
            )

add_executable(cooper
        Sly2Decomp.cpp )

target_link_libraries(cooper
        CooperGame
        CooperLib
        fmt
        cubeb
        zstd
        glfw
        imgui)