#include "ValidationMode.h"

namespace mockfakegen
{
	std::string_view ToString(ValidationMode mode) noexcept
	{
		switch (mode)
		{
			case ValidationMode::None:
				return "none";
			case ValidationMode::Syntax:
				return "syntax";
			case ValidationMode::Compile:
				return "compile";
		}

		return "unknown";
	}

	std::optional<ValidationMode> ParseValidationMode(std::string_view text)
	{
		if (text == "none")
		{
			return ValidationMode::None;
		}
		if (text == "syntax")
		{
			return ValidationMode::Syntax;
		}
		if (text == "compile")
		{
			return ValidationMode::Compile;
		}
		return std::nullopt;
	}
} // namespace mockfakegen
