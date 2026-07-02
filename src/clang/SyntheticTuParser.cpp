#include "clang/SyntheticTuParser.h"

#include <string>
#include <system_error>

#include <clang/Tooling/Tooling.h>

#include "clang/ClangDiagnosticCollector.h"

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute = std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}

			std::error_code canonical_error;
			const auto canonical = std::filesystem::weakly_canonical(absolute, canonical_error);
			if (!canonical_error)
			{
				return canonical.lexically_normal();
			}

			return absolute.lexically_normal();
		}

		[[nodiscard]] std::string IncludeSpelling(const std::filesystem::path& header_path,
												  const std::filesystem::path& project_root)
		{
			std::error_code relative_error;
			const auto relative =
				std::filesystem::relative(header_path, project_root, relative_error);
			if (!relative_error)
			{
				return relative.lexically_normal().generic_string();
			}

			return header_path.filename().generic_string();
		}
	} // namespace

	std::string BuildSyntheticTuCode(std::string include_spelling)
	{
		return "#include \"" + include_spelling + "\"\n";
	}

	std::vector<std::string> BuildSyntheticTuFallbackArgs(const std::filesystem::path& project_root)
	{
		return {
			"-std=c++23",
			"-I" + AbsoluteNormalized(project_root).generic_string(),
		};
	}

	SyntheticTuParseResult ParseHeaderWithSyntheticTu(const SyntheticTuParseOptions& options)
	{
		SyntheticTuParseResult result;
		const auto header_path = AbsoluteNormalized(options.header_path);
		const auto project_root = AbsoluteNormalized(options.project_root);
		const auto include_spelling = IncludeSpelling(header_path, project_root);

		result.header = HeaderParseRecord{
			.header_path = header_path,
			.include_spelling = include_spelling,
			.parsed_by_synthetic_tu = true,
		};
		result.compile_args = options.compile_args.empty()
			? BuildSyntheticTuFallbackArgs(project_root)
			: options.compile_args;
		result.synthetic_code = BuildSyntheticTuCode(include_spelling);

		ClangDiagnosticCollector diagnostic_collector;
		result.ast = clang::tooling::buildASTFromCodeWithArgs(
			result.synthetic_code,
			result.compile_args,
			"mockfakegen_synthetic_tu.cpp",
			"mockfakegen-clang-tool",
			std::make_shared<clang::PCHContainerOperations>(),
			clang::tooling::getClangStripDependencyFileAdjuster(),
			clang::tooling::FileContentMappings(),
			&diagnostic_collector);
		result.diagnostics = diagnostic_collector.diagnostics();

		result.success = result.ast != nullptr && !HasClangErrorDiagnostic(result.diagnostics);
		return result;
	}
} // namespace mockfakegen
