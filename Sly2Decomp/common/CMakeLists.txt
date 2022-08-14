add_library(common
        versions.cpp
        audio/audio_formats.cpp
        dma/dma.cpp
        dma/dma_copy.cpp
        dma/gs.cpp
        log/log.cpp
        math/geometry.cpp
        util/Assert.cpp
        util/BitUtils.cpp
        util/compress.cpp
        util/crc32.cpp
        util/FileUtil.cpp
        util/json_util.cpp
        util/read_iso_file.cpp
        util/SimpleThreadGroup.cpp
        util/Timer.cpp
        util/os.cpp
        util/FontUtils.cpp
        util/FrameLimiter.cpp)
        

target_link_libraries(common fmt libzstd_static)

if(WIN32)
    target_link_libraries(common wsock32 ws2_32 windowsapp)
else()
    target_link_libraries(common stdc++fs)
endif()
