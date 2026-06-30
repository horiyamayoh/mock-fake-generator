#pragma once

#include <string>
#include <vector>

namespace mockfakegen::support
{
	[[nodiscard]] std::vector<std::string> CopyArgv(int argc, const char* const* argv);
} // namespace mockfakegen::support
