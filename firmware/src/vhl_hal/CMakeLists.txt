# firmware/components/vhl_hal/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "i2s_audio_driver.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core
    )
else()
    add_library(vhl_hal STATIC i2s_audio_driver.c)
    target_include_directories(vhl_hal PUBLIC include)
    target_link_libraries(vhl_hal PUBLIC vhl_core)
endif()
