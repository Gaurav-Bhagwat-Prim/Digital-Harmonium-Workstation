# firmware/components/vhl_feature/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_runtime vhl_audio
    )
else()
    add_library(vhl_feature INTERFACE)
    target_include_directories(vhl_feature INTERFACE include)
    target_link_libraries(vhl_feature INTERFACE vhl_core vhl_runtime vhl_audio)
endif()
