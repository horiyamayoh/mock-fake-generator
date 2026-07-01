#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mockfakegen
{
	enum class GeneratedFileKind
	{
		RuntimeHeader,
		MockHeader,
		FakeSource,
		AllMocksHeader,
		CMakeFragment,
		Manifest,
		Report,
	};

	struct GeneratedSourceClass
	{
		std::string qualified_name;
		std::filesystem::path source_header;
		std::size_t generated_method_count = 0U;
		bool link_ready = true;
	};

	struct GeneratedSourceCompileArgs
	{
		std::string qualified_name;
		std::filesystem::path source_header;
		std::filesystem::path compiler;
		std::vector<std::string> args;
	};

	struct GeneratedFile
	{
		std::filesystem::path relative_path;
		std::string content;
		GeneratedFileKind kind = GeneratedFileKind::MockHeader;
		std::optional<GeneratedSourceClass> source_class;
	};

	[[nodiscard]] std::string_view ToString(GeneratedFileKind kind) noexcept;
	[[nodiscard]] std::string EnsureTrailingNewline(std::string content);
	[[nodiscard]] GeneratedFile
	MakeGeneratedFile(std::filesystem::path relative_path,
					  std::string content,
					  GeneratedFileKind kind,
					  std::optional<GeneratedSourceClass> source_class = std::nullopt);

	void SortGeneratedFiles(std::vector<GeneratedFile>& files);
	[[nodiscard]] std::vector<GeneratedFile>
	SortedGeneratedFiles(std::span<const GeneratedFile> files);
} // namespace mockfakegen
