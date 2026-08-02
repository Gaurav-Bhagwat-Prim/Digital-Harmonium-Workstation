// =============================================================================
// DiffCommand.h — VHL Tools: Structure-Level Binary Diff Command
//
// Decoded object comparison between two .vhl files.
// Supports Summary Mode (default) and Detailed Mode (--detailed).
// =============================================================================

#pragma once

#include <string>
#include <vector>

namespace Vhl {
namespace Tools {
namespace Commands {

class DiffCommand
{
public:
    static int execute(const std::vector<std::string>& args);
};

} // namespace Commands
} // namespace Tools
} // namespace Vhl
