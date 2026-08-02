# firmware/components/vhl_runtime/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "system_manager.c" "task_manager.c" "event_bus.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_hal
    )
else()
    add_library(vhl_runtime STATIC system_manager.c task_manager.c event_bus.c)
    target_include_directories(vhl_runtime PUBLIC include)
    target_link_libraries(vhl_runtime PUBLIC vhl_core)
endif()
