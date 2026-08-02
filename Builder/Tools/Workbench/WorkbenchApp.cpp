// =============================================================================
// WorkbenchApp.cpp — Phase 2E: Developer Workbench Implementation
// =============================================================================

#include "WorkbenchApp.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace Vhl {
namespace Workbench {

// Helper to sanitize user input choice (trim whitespace and trailing dot)
static std::string sanitizeChoice(std::string s)
{
    // Remove whitespace
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
    // Remove trailing dot if user typed "1."
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

bool WorkbenchApp::init(const std::string& startPath)
{
    fs::path current = startPath.empty() ? fs::current_path() : fs::path(startPath);

    // Search upwards to find project root containing Instruments/ and Builder/
    fs::path root = current;
    bool foundRoot = false;

    for (int depth = 0; depth < 5; ++depth)
    {
        if (fs::exists(root / "Instruments") && fs::exists(root / "Builder"))
        {
            foundRoot = true;
            break;
        }
        if (root.has_parent_path())
            root = root.parent_path();
        else
            break;
    }

    if (!foundRoot)
    {
        // Default fallback to expected absolute location if searching relative path fails
        root = "D:/TYBSC/TYBSC Arduino Project/Esp32 Digital Musical Workstation";
    }

    _workspaceRoot  = root;
    _instrumentsDir = root / "Instruments";
    _builderExe     = root / "Builder" / "build" / "bin" / "vhl_builder.exe";
    _toolsExe       = root / "Builder" / "build" / "bin" / "vhl_tools.exe";

    discoverInstruments();
    return true;
}

void WorkbenchApp::discoverInstruments()
{
    _instruments.clear();

    if (!fs::exists(_instrumentsDir) || !fs::is_directory(_instrumentsDir))
    {
        return;
    }

    for (const auto& entry : fs::directory_iterator(_instrumentsDir))
    {
        if (!entry.is_directory())
            continue;

        std::string dirName = entry.path().filename().string();

        // Skip samples folder or hidden directories
        if (dirName == "samples" || dirName == "Samples" || dirName[0] == '.')
            continue;

        InstrumentInfo info;
        info.name = dirName;
        info.folderPath = entry.path().string();

        // Scan directory for .sfz files
        std::vector<fs::path> sfzFiles;
        for (const auto& f : fs::directory_iterator(entry.path()))
        {
            if (f.is_regular_file() && f.path().extension() == ".sfz")
            {
                sfzFiles.push_back(f.path());
            }
        }

        if (sfzFiles.size() == 1)
        {
            info.hasSfz = true;
            info.sfzPath = sfzFiles[0].string();

            fs::path vhlPath = sfzFiles[0];
            vhlPath.replace_extension(".vhl");
            info.vhlPath = vhlPath.string();
            info.hasVhl = fs::exists(vhlPath);

            _instruments.push_back(info);
        }
        else if (sfzFiles.size() > 1)
        {
            // Pick first SFZ
            info.hasSfz = true;
            info.sfzPath = sfzFiles[0].string();

            fs::path vhlPath = sfzFiles[0];
            vhlPath.replace_extension(".vhl");
            info.vhlPath = vhlPath.string();
            info.hasVhl = fs::exists(vhlPath);

            _instruments.push_back(info);
        }
    }

    // Sort instruments alphabetically by name
    std::sort(_instruments.begin(), _instruments.end(),
              [](const InstrumentInfo& a, const InstrumentInfo& b) {
                  return a.name < b.name;
              });
}

void WorkbenchApp::run()
{
    while (true)
    {
        discoverInstruments();

        std::cout << "\n========================================\n";
        std::cout << "VHL Developer Workbench\n";
        std::cout << "========================================\n";
        std::cout << "Workspace : " << _workspaceRoot.string() << "\n";
        std::cout << "----------------------------------------\n";
        std::cout << "Available Instruments:\n";

        if (_instruments.empty())
        {
            std::cout << "  (No instrument directories found in Instruments/)\n";
        }
        else
        {
            for (size_t i = 0; i < _instruments.size(); ++i)
            {
                const auto& inst = _instruments[i];
                std::cout << "  " << (i + 1) << ". " << inst.name;
                if (inst.hasVhl) std::cout << " [.vhl ready]";
                std::cout << "\n";
            }
        }

        std::cout << "----------------------------------------\n";
        std::cout << "  B. Build Builder & Tools\n";
        std::cout << "  R. Refresh Instruments\n";
        std::cout << "  Q. Quit\n";
        std::cout << "----------------------------------------\n";
        std::cout << "Select option: ";

        std::string rawInput;
        if (!(std::cin >> rawInput))
            break;

        std::string choice = sanitizeChoice(rawInput);

        if (choice == "Q" || choice == "q")
        {
            std::cout << "Exiting Developer Workbench.\n";
            break;
        }
        else if (choice == "B" || choice == "b")
        {
            actionBuildBuilder();
        }
        else if (choice == "R" || choice == "r")
        {
            discoverInstruments();
            std::cout << "[INFO] Rescanned Instruments directory.\n";
        }
        else
        {
            try
            {
                size_t idx = std::stoul(choice);
                if (idx >= 1 && idx <= _instruments.size())
                {
                    showInstrumentMenu(idx - 1);
                }
                else
                {
                    std::cout << "[ERROR] Invalid selection.\n";
                }
            }
            catch (...)
            {
                std::cout << "[ERROR] Invalid input.\n";
            }
        }
    }
}

void WorkbenchApp::showInstrumentMenu(size_t index)
{
    while (true)
    {
        discoverInstruments();
        const auto& inst = _instruments[index];

        std::cout << "\n========================================\n";
        std::cout << "Instrument: " << inst.name << "\n";
        std::cout << "========================================\n";
        std::cout << "  SFZ : " << (inst.hasSfz ? inst.sfzPath : "MISSING") << "\n";
        std::cout << "  VHL : " << (inst.hasVhl ? inst.vhlPath : "NOT BUILT") << "\n";
        std::cout << "----------------------------------------\n";
        std::cout << "  1. Build Instrument (Build -> Validate -> Inspect)\n";
        std::cout << "  2. Inspect\n";
        std::cout << "  3. Validate\n";
        std::cout << "  4. Diff\n";
        std::cout << "  5. Open Instrument Folder\n";
        std::cout << "  6. Back to Main Menu\n";
        std::cout << "========================================\n";
        std::cout << "Select option: ";

        std::string rawInput;
        if (!(std::cin >> rawInput))
            break;

        std::string choice = sanitizeChoice(rawInput);

        if (choice == "1")
        {
            actionBuildInstrument(inst);
        }
        else if (choice == "2")
        {
            actionInspect(inst);
        }
        else if (choice == "3")
        {
            actionValidate(inst);
        }
        else if (choice == "4")
        {
            actionDiff(inst);
        }
        else if (choice == "5")
        {
            actionOpenFolder(inst);
        }
        else if (choice == "6")
        {
            break;
        }
        else
        {
            std::cout << "[ERROR] Invalid option.\n";
        }
    }
}

int WorkbenchApp::executeCommand(const std::string& cmd) const
{
    std::cout << "\n[EXEC] " << cmd << "\n";
    std::cout << "-------------------------------------------------------------\n";

#if defined(_WIN32) || defined(_WIN64)
    // Windows cmd.exe /c strips outer quotes if string starts & ends with double quotes.
    // Wrapping in extra double quotes ensures inner path quotes remain intact.
    std::string sysCmd = "\"" + cmd + "\"";
    int code = std::system(sysCmd.c_str());
#else
    int code = std::system(cmd.c_str());
#endif

    std::cout << "-------------------------------------------------------------\n";
    return code;
}

void WorkbenchApp::actionBuildInstrument(const InstrumentInfo& inst)
{
    if (!fs::exists(_builderExe))
    {
        std::cout << "[ERROR] Builder executable not found at: " << _builderExe.string() << "\n";
        std::cout << "        Please compile the builder first (Option B from main menu).\n";
        return;
    }
    if (!inst.hasSfz)
    {
        std::cout << "[ERROR] No .sfz file found for instrument: " << inst.name << "\n";
        return;
    }

    // Step 1: Run vhl_builder
    std::string cmdBuilder = "\"" + _builderExe.string() + "\" \"" + inst.sfzPath + "\"";
    int res = executeCommand(cmdBuilder);

    if (res != 0)
    {
        std::cout << "[FAIL] Building instrument failed with exit code " << res << "\n";
        return;
    }

    std::cout << "[SUCCESS] Instrument compilation finished.\n";

    // Step 2: Auto-run vhl_tools validate
    if (fs::exists(_toolsExe))
    {
        std::string cmdValidate = "\"" + _toolsExe.string() + "\" validate \"" + inst.vhlPath + "\"";
        int valRes = executeCommand(cmdValidate);

        if (valRes == 0)
        {
            // Step 3: Auto-run vhl_tools inspect
            std::string cmdInspect = "\"" + _toolsExe.string() + "\" inspect \"" + inst.vhlPath + "\"";
            executeCommand(cmdInspect);
        }
        else
        {
            std::cout << "[WARNING] Validation returned errors/failures; skipping auto-inspect.\n";
        }
    }
}

void WorkbenchApp::actionInspect(const InstrumentInfo& inst)
{
    if (!fs::exists(_toolsExe))
    {
        std::cout << "[ERROR] Tools executable not found at: " << _toolsExe.string() << "\n";
        return;
    }
    if (!fs::exists(inst.vhlPath))
    {
        std::cout << "[ERROR] Compiled .vhl file does not exist: " << inst.vhlPath << "\n";
        std::cout << "        Please build the instrument first.\n";
        return;
    }

    std::string cmd = "\"" + _toolsExe.string() + "\" inspect \"" + inst.vhlPath + "\" --all";
    executeCommand(cmd);
}

void WorkbenchApp::actionValidate(const InstrumentInfo& inst)
{
    if (!fs::exists(_toolsExe))
    {
        std::cout << "[ERROR] Tools executable not found at: " << _toolsExe.string() << "\n";
        return;
    }
    if (!fs::exists(inst.vhlPath))
    {
        std::cout << "[ERROR] Compiled .vhl file does not exist: " << inst.vhlPath << "\n";
        std::cout << "        Please build the instrument first.\n";
        return;
    }

    std::string cmd = "\"" + _toolsExe.string() + "\" validate \"" + inst.vhlPath + "\" --strict";
    executeCommand(cmd);
}

void WorkbenchApp::actionDiff(const InstrumentInfo& inst)
{
    if (!fs::exists(_toolsExe))
    {
        std::cout << "[ERROR] Tools executable not found at: " << _toolsExe.string() << "\n";
        return;
    }
    if (!fs::exists(inst.vhlPath))
    {
        std::cout << "[ERROR] Primary .vhl file does not exist: " << inst.vhlPath << "\n";
        return;
    }

    std::cout << "\nChoose second instrument to diff against:\n";
    std::vector<std::string> otherPaths;

    for (size_t i = 0; i < _instruments.size(); ++i)
    {
        if (_instruments[i].hasVhl)
        {
            std::cout << "  " << (otherPaths.size() + 1) << ". " << _instruments[i].name << " (" << _instruments[i].vhlPath << ")\n";
            otherPaths.push_back(_instruments[i].vhlPath);
        }
    }

    if (otherPaths.empty())
    {
        std::cout << "[ERROR] No other compiled .vhl instruments found in workspace.\n";
        return;
    }

    std::cout << "Select second instrument number: ";
    std::string rawInput;
    if (std::cin >> rawInput)
    {
        std::string pickChoice = sanitizeChoice(rawInput);
        try
        {
            size_t pick = std::stoul(pickChoice);
            if (pick >= 1 && pick <= otherPaths.size())
            {
                std::string cmd = "\"" + _toolsExe.string() + "\" diff \"" + inst.vhlPath + "\" \"" + otherPaths[pick - 1] + "\" --detailed";
                executeCommand(cmd);
                return;
            }
        }
        catch (...) {}
    }
    std::cout << "[ERROR] Invalid selection.\n";
}

void WorkbenchApp::actionOpenFolder(const InstrumentInfo& inst)
{
    std::cout << "[INFO] Opening instrument folder: " << inst.folderPath << "\n";
#if defined(_WIN32) || defined(_WIN64)
    std::string cmd = "explorer \"" + inst.folderPath + "\"";
    std::system(cmd.c_str());
#elif defined(__APPLE__)
    std::string cmd = "open \"" + inst.folderPath + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + inst.folderPath + "\"";
    std::system(cmd.c_str());
#endif
}

void WorkbenchApp::actionBuildBuilder()
{
    fs::path builderDir = _workspaceRoot / "Builder";
    std::cout << "[INFO] Building VHL Builder & Tools in: " << builderDir.string() << "\n";

    std::string cmd = "cmake --build \"" + (builderDir / "build").string() + "\" --config Release";
    int res = executeCommand(cmd);

    if (res == 0)
    {
        std::cout << "\n========================================\n";
        std::cout << "Builder & Tools compiled successfully.\n";
        std::cout << "========================================\n";
    }
    else
    {
        std::cout << "\n========================================\n";
        std::cout << "Compilation failed.\n";
        std::cout << "========================================\n";
    }
}

} // namespace Workbench
} // namespace Vhl
