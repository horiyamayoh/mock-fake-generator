#include "model/GeneratedFile.h"

#include <algorithm>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] int KindRank(GeneratedFileKind kind) noexcept
		{
			switch (kind)
			{
				case GeneratedFileKind::RuntimeHeader:
					return 0;
				case GeneratedFileKind::MockHeader:
					return 1;
				case GeneratedFileKind::FakeSource:
					return 2;
				case GeneratedFileKind::AllMocksHeader:
					return 3;
				case GeneratedFileKind::CMakeFragment:
					return 4;
				case GeneratedFileKind::Manifest:
					return 5;
				case GeneratedFileKind::Report:
					return 6;
			}

			return 7;
		}

		[[nodiscard]] std::string SourceClassNameOrEmpty(const GeneratedFile& file)
		{
			if (!file.source_class.has_value())
			{
				return {};
			}

			return file.source_class->qualified_name;
		}

		[[nodiscard]] bool GeneratedFileLess(const GeneratedFile& lhs, const GeneratedFile& rhs)
		{
			const auto lhs_path = lhs.relative_path.generic_string();
			const auto rhs_path = rhs.relative_path.generic_string();
			if (lhs_path != rhs_path)
			{
				return lhs_path < rhs_path;
			}

			const auto lhs_kind = KindRank(lhs.kind);
			const auto rhs_kind = KindRank(rhs.kind);
			if (lhs_kind != rhs_kind)
			{
				return lhs_kind < rhs_kind;
			}

			return SourceClassNameOrEmpty(lhs) < SourceClassNameOrEmpty(rhs);
		}
	} // namespace

	std::string_view ToString(GeneratedFileKind kind) noexcept
	{
		switch (kind)
		{
			case GeneratedFileKind::RuntimeHeader:
				return "runtime_header";
			case GeneratedFileKind::MockHeader:
				return "mock_header";
			case GeneratedFileKind::FakeSource:
				return "fake_source";
			case GeneratedFileKind::AllMocksHeader:
				return "all_mocks_header";
			case GeneratedFileKind::CMakeFragment:
				return "cmake_fragment";
			case GeneratedFileKind::Manifest:
				return "manifest";
			case GeneratedFileKind::Report:
				return "report";
		}

		return "unknown";
	}

	std::string EnsureTrailingNewline(std::string content)
	{
		if (content.empty() || content.back() != '\n')
		{
			content.push_back('\n');
		}

		return content;
	}

	GeneratedFile MakeGeneratedFile(std::filesystem::path relative_path,
									std::string content,
									GeneratedFileKind kind,
									std::optional<GeneratedSourceClass> source_class)
	{
		return GeneratedFile{
			.relative_path = relative_path.lexically_normal(),
			.content = EnsureTrailingNewline(std::move(content)),
			.kind = kind,
			.source_class = std::move(source_class),
		};
	}

	void SortGeneratedFiles(std::vector<GeneratedFile>& files)
	{
		std::stable_sort(files.begin(), files.end(), GeneratedFileLess);
	}

	std::vector<GeneratedFile> SortedGeneratedFiles(std::span<const GeneratedFile> files)
	{
		std::vector<GeneratedFile> sorted(files.begin(), files.end());
		SortGeneratedFiles(sorted);
		return sorted;
	}
} // namespace mockfakegen
