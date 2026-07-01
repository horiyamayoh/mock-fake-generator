#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mockfakegen::support
{
	[[nodiscard]] std::vector<std::string> CopyArgv(int argc, const char* const* argv);
	[[nodiscard]] std::optional<int> ParsePositiveInt(std::string_view text) noexcept;
	[[nodiscard]] std::optional<bool> ParseStrictBool(std::string_view text) noexcept;
} // namespace mockfakegen::support
