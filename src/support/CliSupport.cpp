#include "support/CliSupport.h"

#include "ket_cli.h"

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
} // namespace mockfakegen::support
