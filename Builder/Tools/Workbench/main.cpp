// =============================================================================
// main.cpp — Phase 2E: Developer Workbench CLI Entry Point
// =============================================================================

#include "WorkbenchApp.h"
#include <iostream>

using namespace Vhl::Workbench;

int main(int argc, char* argv[])
{
    std::string startDir;
    if (argc >= 2)
    {
        startDir = argv[1];
    }

    WorkbenchApp app;
    if (!app.init(startDir))
    {
        std::cerr << "[FATAL] Failed to initialize VHL Developer Workbench.\n";
        return 1;
    }

    app.run();
    return 0;
}
