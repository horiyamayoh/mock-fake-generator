#include "support/CliSupport.h"

#include <limits>

#include "ket_cli.h"
#include "ket_parse.h"

namespace mockfakegen::support
{
	std::vector<std::string> CopyArgv(int argc, const char* const* argv)
	{
		const ket::cli::ArgvView view(argc, argv);
		std::vector<std::string> arguments;
		arguments.reserve(view.Size());

		for (std::size_t index = 0U; index < view.Size(); ++index)
		{
			arguments.emplace_back(view.AtOrEmpty(index));
		}

		if (arguments.empty())
		{
			arguments.emplace_back("mockfakegen");
		}

		return arguments;
	}

	std::optional<int> ParsePositiveInt(std::string_view text) noexcept
	{
		const auto value = ket::parse::UInt<unsigned>(text);
		if (!value.has_value() || *value == 0U ||
			*value > static_cast<unsigned>(std::numeric_limits<int>::max()))
		{
			return std::nullopt;
		}

		return static_cast<int>(*value);
	}

	std::optional<bool> ParseStrictBool(std::string_view text) noexcept
	{
		if (text != "true" && text != "false")
		{
			return std::nullopt;
		}

		return ket::parse::Bool(text);
	}
} // namespace mockfakegen::support
