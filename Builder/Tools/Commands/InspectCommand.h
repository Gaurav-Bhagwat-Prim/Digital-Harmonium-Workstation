// =============================================================================
// InspectCommand.h — VHL Tools: Rich Instrument Inspector Command
//
// Computes and renders rich instrument stats, memory distribution %,
// parameter averages, largest samples ranking, key coverage maps, and section detail tables.
// =============================================================================

#pragma once

#include <string>
#include <vector>

namespace Vhl {
namespace Tools {
namespace Commands {

class InspectCommand
{
public:
    static int execute(const std::vector<std::string>& args);
};

} // namespace Commands
} // namespace Tools
} // namespace Vhl
