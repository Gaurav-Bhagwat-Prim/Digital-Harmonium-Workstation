# firmware/components/vhl_storage/CMakeLists.txt
# Stage 11: recorder_engine.c (VHR recording)
# Stage 12: vhr_player.c (VHR playback — requires vhl_runtime for Event Bus injection)
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "vhl_loader.c" "recorder_engine.c" "vhr_player.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_runtime
    )
else()
    add_library(vhl_storage STATIC vhl_loader.c recorder_engine.c vhr_player.c)
    target_include_directories(vhl_storage PUBLIC include)
    target_link_libraries(vhl_storage PUBLIC vhl_core vhl_runtime)
endif()
