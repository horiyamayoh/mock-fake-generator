#pragma once

#include <optional>
#include <string_view>

namespace mockfakegen
{
	enum class ValidationMode
	{
		None,
		Syntax,
		Compile,
	};

	[[nodiscard]] std::string_view ToString(ValidationMode mode) noexcept;
	[[nodiscard]] std::optional<ValidationMode> ParseValidationMode(std::string_view text);
} // namespace mockfakegen
