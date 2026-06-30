#include "FormatStyle.h"

namespace mockfakegen
{
	std::string_view ToString(FormatStyleKind style) noexcept
	{
		switch (style)
		{
			case FormatStyleKind::File:
				return "file";
			case FormatStyleKind::Llvm:
				return "llvm";
			case FormatStyleKind::Google:
				return "google";
			case FormatStyleKind::None:
				return "none";
		}

		return "unknown";
	}

	std::optional<FormatStyleKind> ParseFormatStyleKind(std::string_view text)
	{
		if (text == "file")
		{
			return FormatStyleKind::File;
		}
		if (text == "llvm")
		{
			return FormatStyleKind::Llvm;
		}
		if (text == "google")
		{
			return FormatStyleKind::Google;
		}
		if (text == "none")
		{
			return FormatStyleKind::None;
		}
		return std::nullopt;
	}
} // namespace mockfakegen
