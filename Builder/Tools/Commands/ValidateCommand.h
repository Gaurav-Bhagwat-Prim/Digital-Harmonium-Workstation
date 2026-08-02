// =============================================================================
// ValidateCommand.h — VHL Tools: Logical Integrity & Quality Score Validator Command
//
// Performs structural & logical checks on .vhl files, categorizes diagnostics
// into INFO, WARNING, and ERROR, computes Quality Score %, and returns exit code 0 or 1.
// =============================================================================

#pragma once

#include <string>
#include <vector>

namespace Vhl {
namespace Tools {
namespace Commands {

class ValidateCommand
{
public:
    static int execute(const std::vector<std::string>& args);
};

} // namespace Commands
} // namespace Tools
} // namespace Vhl
