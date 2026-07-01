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
			case ValidationMode::Link:
				return "link";
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
		if (text == "link")
		{
			return ValidationMode::Link;
		}
		return std::nullopt;
	}
} // namespace mockfakegen
