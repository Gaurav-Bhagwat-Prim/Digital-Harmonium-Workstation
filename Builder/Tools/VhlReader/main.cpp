// =============================================================================
// main.cpp — VHL Reader CLI Entry Point
//
// Standalone Phase 2C Verification Tool.
// Usage: vhl_reader <instrument.vhl>
// =============================================================================

#include <iostream>
#include <string>
#include "VhlReader.h"

using namespace Vhl::Tools;

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: vhl_reader <instrument.vhl>\n";
        std::cerr << "       Lightweight verification tool for .vhl binary files.\n";
        return 1;
    }

    std::string vhlPath = argv[1];

    VhlReader reader;
    if (!reader.load(vhlPath))
    {
        std::cerr << "[FATAL] Could not load VHL file: " << vhlPath << "\n";
        return 1;
    }

    reader.printAll();

    std::vector<std::string> warnings, errors;
    bool valid = reader.verifyIntegrity(warnings, errors);

    return valid ? 0 : 1;
}
