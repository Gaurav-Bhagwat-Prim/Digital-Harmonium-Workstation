// =============================================================================
// WorkbenchApp.h — Phase 2E: Developer Workbench (Workflow Automation)
//
// Single entry point CLI application for discovery, building, validation,
// inspection, diffing, and folder navigation across VHL instruments.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Vhl {
namespace Workbench {

struct InstrumentInfo
{
    std::string name;
    std::string folderPath;
    std::string sfzPath;
    std::string vhlPath;
    bool        hasSfz = false;
    bool        hasVhl = false;
};

class WorkbenchApp
{
public:
    WorkbenchApp() = default;

    /// Initialize workspace paths and perform initial instrument discovery
    bool init(const std::string& startPath = "");

    /// Main event loop
    void run();

private:
    std::filesystem::path     _workspaceRoot;
    std::filesystem::path     _instrumentsDir;
    std::filesystem::path     _builderExe;
    std::filesystem::path     _toolsExe;

    std::vector<InstrumentInfo> _instruments;

    /// Discover instruments inside Instruments/ directory
    void discoverInstruments();

    /// Menu renderers & handlers
    void showMainMenu();
    void showInstrumentMenu(size_t index);

    /// Action handlers
    void actionBuildInstrument(const InstrumentInfo& inst);
    void actionInspect(const InstrumentInfo& inst);
    void actionValidate(const InstrumentInfo& inst);
    void actionDiff(const InstrumentInfo& inst);
    void actionOpenFolder(const InstrumentInfo& inst);
    void actionBuildBuilder();

    /// Process execution helper
    int executeCommand(const std::string& cmd) const;
};

} // namespace Workbench
} // namespace Vhl
