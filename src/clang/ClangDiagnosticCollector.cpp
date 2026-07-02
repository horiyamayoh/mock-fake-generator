#include "clang/ClangDiagnosticCollector.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/SmallString.h>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] ClangDiagnosticSeverity
		ToSeverity(clang::DiagnosticsEngine::Level level) noexcept
		{
			switch (level)
			{
				case clang::DiagnosticsEngine::Ignored:
				case clang::DiagnosticsEngine::Note:
				case clang::DiagnosticsEngine::Remark:
					return ClangDiagnosticSeverity::Note;
				case clang::DiagnosticsEngine::Warning:
					return ClangDiagnosticSeverity::Warning;
				case clang::DiagnosticsEngine::Error:
				case clang::DiagnosticsEngine::Fatal:
					return ClangDiagnosticSeverity::Error;
			}

			return ClangDiagnosticSeverity::Error;
		}

		[[nodiscard]] SourceLocation ToSourceLocation(const clang::SourceManager& source_manager,
													  clang::SourceLocation location)
		{
			if (location.isInvalid())
			{
				return {};
			}

			const auto expansion_location = source_manager.getExpansionLoc(location);
			if (expansion_location.isInvalid())
			{
				return {};
			}

			const auto presumed = source_manager.getPresumedLoc(expansion_location);
			if (presumed.isInvalid())
			{
				return {};
			}

			return SourceLocation{
				.file = std::filesystem::path(presumed.getFilename()),
				.line = presumed.getLine(),
				.column = presumed.getColumn(),
			};
		}

		[[nodiscard]] SourceRange ToSourceRange(const clang::Diagnostic& info)
		{
			if (!info.hasSourceManager())
			{
				return {};
			}

			auto& source_manager = info.getSourceManager();
			const auto location = ToSourceLocation(source_manager, info.getLocation());
			if (location.file.empty())
			{
				return {};
			}

			return SourceRange{
				.begin = location,
				.end = location,
			};
		}

		[[nodiscard]] std::string FormatMessage(const clang::Diagnostic& info)
		{
			llvm::SmallString<256> message;
			info.FormatDiagnostic(message);
			return std::string(message.begin(), message.end());
		}

		[[nodiscard]] bool HasLocation(const SourceRange& range)
		{
			return !range.begin.file.empty() && range.begin.line != 0U;
		}
	} // namespace

	void ClangDiagnosticCollector::HandleDiagnostic(clang::DiagnosticsEngine::Level level,
													const clang::Diagnostic& info)
	{
		clang::DiagnosticConsumer::HandleDiagnostic(level, info);
		diagnostics_.push_back(ClangParseDiagnostic{
			.severity = ToSeverity(level),
			.source_range = ToSourceRange(info),
			.message = FormatMessage(info),
		});
	}

	const std::vector<ClangParseDiagnostic>& ClangDiagnosticCollector::diagnostics() const noexcept
	{
		return diagnostics_;
	}

	bool HasClangErrorDiagnostic(std::span<const ClangParseDiagnostic> diagnostics)
	{
		return std::any_of(diagnostics.begin(),
						   diagnostics.end(),
						   [](const auto& diagnostic)
						   {
							   return diagnostic.severity == ClangDiagnosticSeverity::Error;
						   });
	}

	std::string ClangDiagnosticSeverityName(ClangDiagnosticSeverity severity)
	{
		switch (severity)
		{
			case ClangDiagnosticSeverity::Error:
				return "error";
			case ClangDiagnosticSeverity::Warning:
				return "warning";
			case ClangDiagnosticSeverity::Note:
				return "note";
		}

		return "unknown";
	}

	std::string ClangDiagnosticsSummary(std::span<const ClangParseDiagnostic> diagnostics)
	{
		std::string summary;
		for (const auto& diagnostic : diagnostics)
		{
			if (!summary.empty())
			{
				summary += '\n';
			}
			summary += ClangDiagnosticSeverityName(diagnostic.severity);
			summary += ": ";
			summary += diagnostic.message;
		}
		return summary;
	}

	SourceRange PrimaryClangDiagnosticSourceRange(std::span<const ClangParseDiagnostic> diagnostics)
	{
		const auto error_with_location =
			std::find_if(diagnostics.begin(),
						 diagnostics.end(),
						 [](const auto& diagnostic)
						 {
							 return diagnostic.severity == ClangDiagnosticSeverity::Error &&
								 HasLocation(diagnostic.source_range);
						 });
		if (error_with_location != diagnostics.end())
		{
			return error_with_location->source_range;
		}

		const auto any_with_location = std::find_if(diagnostics.begin(),
													diagnostics.end(),
													[](const auto& diagnostic)
													{
														return HasLocation(diagnostic.source_range);
													});
		if (any_with_location != diagnostics.end())
		{
			return any_with_location->source_range;
		}

		return {};
	}
} // namespace mockfakegen
