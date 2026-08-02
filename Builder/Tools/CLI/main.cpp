// =============================================================================
// main.cpp — VHL Tool Suite Unified CLI Entry Point (vhl_tools)
//
// Subcommand router for inspect, validate, diff, and help.
// Usage:
//   vhl_tools inspect  <file.vhl> [options]
//   vhl_tools validate <file.vhl> [--strict]
//   vhl_tools diff     <old.vhl> <new.vhl> [--detailed]
// =============================================================================

#include <iostream>
#include <string>
#include <vector>

#include "../Commands/InspectCommand.h"
#include "../Commands/ValidateCommand.h"
#include "../Commands/DiffCommand.h"

using namespace Vhl::Tools::Commands;

static void printHelp()
{
    std::cout << "=============================================================\n";
    std::cout << "VHL Tool Suite v2.0 (vhl_tools)\n";
    std::cout << "Unified developer & debugging toolkit for the VHL binary format\n";
    std::cout << "=============================================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  vhl_tools inspect  <file.vhl> [options]\n";
    std::cout << "        Inspect header, memory footprint %, key coverage map,\n";
    std::cout << "        instrument stats, largest samples, envelopes & filters.\n";
    std::cout << "        Options: --all, --regions, --samples\n\n";
    std::cout << "  vhl_tools validate <file.vhl> [--strict]\n";
    std::cout << "        Verify structural integrity, layout non-overlap, CRC32,\n";
    std::cout << "        parameter bounds, and compute Quality Score %.\n";
    std::cout << "        Returns exit code 0 (PASS) or 1 (FAIL).\n\n";
    std::cout << "  vhl_tools diff     <old.vhl> <new.vhl> [--detailed]\n";
    std::cout << "        Compare two VHL binary files at the decoded object level.\n";
    std::cout << "        Default mode prints summary deltas; --detailed shows exact\n";
    std::cout << "        parameter changes per region.\n\n";
    std::cout << "  vhl_tools help\n";
    std::cout << "        Display this help message.\n\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printHelp();
        return 1;
    }

    std::string subcommand = argv[1];
    std::vector<std::string> subArgs;
    for (int i = 2; i < argc; ++i)
    {
        subArgs.push_back(argv[i]);
    }

    if (subcommand == "inspect")
    {
        return InspectCommand::execute(subArgs);
    }
    else if (subcommand == "validate")
    {
        return ValidateCommand::execute(subArgs);
    }
    else if (subcommand == "diff")
    {
        return DiffCommand::execute(subArgs);
    }
    else if (subcommand == "help" || subcommand == "--help" || subcommand == "-h")
    {
        printHelp();
        return 0;
    }
    else
    {
        std::cerr << "[ERROR] Unknown subcommand: '" << subcommand << "'\n\n";
        printHelp();
        return 1;
    }
}
