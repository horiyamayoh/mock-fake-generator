#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "FormatStyle.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	struct GeneratedFormatOptions
	{
		FormatStyleKind style = FormatStyleKind::File;
		std::filesystem::path style_search_root;
	};

	struct GeneratedFormatDiagnostic
	{
		std::filesystem::path path;
		std::string message;
	};

	struct GeneratedFormatResult
	{
		std::vector<GeneratedFile> files;
		std::vector<GeneratedFormatDiagnostic> diagnostics;

		[[nodiscard]] bool ok() const noexcept
		{
			return diagnostics.empty();
		}
	};

	[[nodiscard]] GeneratedFormatResult FormatGeneratedFiles(const GeneratedFormatOptions& options,
															 std::span<const GeneratedFile> files);
} // namespace mockfakegen
