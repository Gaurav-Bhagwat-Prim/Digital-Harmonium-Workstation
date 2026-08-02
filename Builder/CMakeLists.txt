cmake_minimum_required(VERSION 3.16)
project(VhlBuilder VERSION 2.0.0 LANGUAGES CXX)

# ==============================================================================
# C++ Standard: C++17 required for std::optional, std::filesystem, if-init
# ==============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Compiler warnings
# ==============================================================================
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ==============================================================================
# Source groups for IDE organization
# ==============================================================================

set(BOM_SOURCES
    BuilderObjectModel/BomTypes.h
    BuilderObjectModel/BomEnvelope.h
    BuilderObjectModel/BomFilter.h
    BuilderObjectModel/BomLFO.h
    BuilderObjectModel/BomRegion.h
    BuilderObjectModel/BomInstrument.h
    BuilderObjectModel/BuilderProject.h
    BuilderObjectModel/ProcessedSample.h
    BuilderObjectModel/ProcessedRegion.h
    BuilderObjectModel/ProcessedProject.h
)

set(FRONTEND_SOURCES
    Frontend/ParseError.h
    Frontend/NoteParser.h
    Frontend/NoteParser.cpp
    Frontend/SfzLexer.h
    Frontend/SfzLexer.cpp
    Frontend/SfzParser.h
    Frontend/SfzParser.cpp
)

set(ASSET_SOURCES
    AssetProcessing/WavLoader.h
    AssetProcessing/WavLoader.cpp
    AssetProcessing/ParameterConverter.h
    AssetProcessing/LookupTableBuilder.h
    AssetProcessing/LookupTableBuilder.cpp
    AssetProcessing/AssetProcessor.h
    AssetProcessing/AssetProcessor.cpp
)

set(BACKEND_SOURCES
    Backend/CRC32.h
    Backend/CRC32.cpp
    Backend/BinaryWriter.h
    Backend/BinaryWriter.cpp
    Backend/LayoutPlanner.h
    Backend/LayoutPlanner.cpp
    Backend/Serializer.h
    Backend/Serializer.cpp
)

set(VHL_CORE_SOURCES
    Tools/Core/VhlDocument.h
    Tools/Core/VhlDocument.cpp
    Tools/Core/VhlLoader.h
    Tools/Core/VhlLoader.cpp
    Tools/Core/VhlFormatter.h
    Tools/Core/VhlFormatter.cpp
)

set(VHL_COMMANDS_SOURCES
    Tools/Commands/InspectCommand.h
    Tools/Commands/InspectCommand.cpp
    Tools/Commands/ValidateCommand.h
    Tools/Commands/ValidateCommand.cpp
    Tools/Commands/DiffCommand.h
    Tools/Commands/DiffCommand.cpp
)

set(WORKBENCH_SOURCES
    Tools/Workbench/WorkbenchApp.h
    Tools/Workbench/WorkbenchApp.cpp
    Tools/Workbench/main.cpp
)

set(READER_SOURCES
    Tools/VhlReader/VhlReader.h
    Tools/VhlReader/VhlReader.cpp
    Tools/VhlReader/main.cpp
)

# ==============================================================================
# Main executable (vhl_builder)
# ==============================================================================

add_executable(vhl_builder
    main.cpp
    ${BOM_SOURCES}
    ${FRONTEND_SOURCES}
    ${ASSET_SOURCES}
    ${BACKEND_SOURCES}
)

target_include_directories(vhl_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../phase1_vhl
)

# ==============================================================================
# Unified Phase 2D Tools executable (vhl_tools)
# ==============================================================================

add_executable(vhl_tools
    Tools/CLI/main.cpp
    ${VHL_CORE_SOURCES}
    ${VHL_COMMANDS_SOURCES}
    Backend/CRC32.h
    Backend/CRC32.cpp
)

target_include_directories(vhl_tools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../phase1_vhl
)

# ==============================================================================
# Phase 2E Developer Workbench executable (vhl_workbench)
# ==============================================================================

add_executable(vhl_workbench
    ${WORKBENCH_SOURCES}
)

target_include_directories(vhl_workbench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==============================================================================
# Legacy verification prototype (vhl_reader)
# ==============================================================================

add_executable(vhl_reader
    ${READER_SOURCES}
    Backend/CRC32.h
    Backend/CRC32.cpp
)

target_include_directories(vhl_reader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../phase1_vhl
)

# std::filesystem linking
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(vhl_builder    PRIVATE stdc++fs)
    target_link_libraries(vhl_tools      PRIVATE stdc++fs)
    target_link_libraries(vhl_workbench  PRIVATE stdc++fs)
    target_link_libraries(vhl_reader     PRIVATE stdc++fs)
endif()

# ==============================================================================
# Source groups for IDE organization
# ==============================================================================

source_group("BuilderObjectModel" FILES ${BOM_SOURCES})
source_group("Frontend"           FILES ${FRONTEND_SOURCES})
source_group("AssetProcessing"    FILES ${ASSET_SOURCES})
source_group("Backend"            FILES ${BACKEND_SOURCES})
source_group("Tools/Core"         FILES ${VHL_CORE_SOURCES})
source_group("Tools/Commands"     FILES ${VHL_COMMANDS_SOURCES})
source_group("Tools/Workbench"    FILES ${WORKBENCH_SOURCES})
source_group("Tools/VhlReader"    FILES ${READER_SOURCES})
source_group("Root"               FILES main.cpp)

# ==============================================================================
# Output directory
# ==============================================================================
set_target_properties(vhl_builder vhl_tools vhl_workbench vhl_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ==============================================================================
# Install
# ==============================================================================
install(TARGETS vhl_builder vhl_tools vhl_workbench vhl_reader DESTINATION bin)

message(STATUS "VHL Toolchain v${PROJECT_VERSION} — Full Ecosystem: Builder + Tools + Workbench")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
