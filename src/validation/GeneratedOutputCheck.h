#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mockfakegen
{
	struct GeneratedOutputTokenDiagnostic
	{
		std::filesystem::path path;
		std::string token;
		std::string message;
	};

	struct GeneratedOutputCheckResult
	{
		std::vector<GeneratedOutputTokenDiagnostic> diagnostics;
		std::size_t checked_file_count = 0U;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] const std::vector<std::string>& ForbiddenGeneratedOutputKetTokens();
	[[nodiscard]] GeneratedOutputCheckResult
	CheckGeneratedOutputForKetTokens(const std::vector<std::filesystem::path>& roots);
} // namespace mockfakegen
