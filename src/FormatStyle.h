#pragma once

#include <optional>
#include <string_view>

namespace mockfakegen
{
	enum class FormatStyleKind
	{
		File,
		Llvm,
		Google,
		None,
	};

	[[nodiscard]] std::string_view ToString(FormatStyleKind style) noexcept;
	[[nodiscard]] std::optional<FormatStyleKind> ParseFormatStyleKind(std::string_view text);
} // namespace mockfakegen
