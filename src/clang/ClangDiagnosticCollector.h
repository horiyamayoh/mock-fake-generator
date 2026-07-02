#pragma once

#include <span>
#include <string>
#include <vector>

#include <clang/Basic/Diagnostic.h>

#include "clang/SyntheticTuParser.h"

namespace mockfakegen
{
	class ClangDiagnosticCollector final : public clang::DiagnosticConsumer
	{
	  public:
		void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
							  const clang::Diagnostic& info) override;

		[[nodiscard]] const std::vector<ClangParseDiagnostic>& diagnostics() const noexcept;

	  private:
		std::vector<ClangParseDiagnostic> diagnostics_;
	};

	[[nodiscard]] bool HasClangErrorDiagnostic(std::span<const ClangParseDiagnostic> diagnostics);
	[[nodiscard]] std::string ClangDiagnosticSeverityName(ClangDiagnosticSeverity severity);
	[[nodiscard]] std::string
	ClangDiagnosticsSummary(std::span<const ClangParseDiagnostic> diagnostics);
	[[nodiscard]] SourceRange
	PrimaryClangDiagnosticSourceRange(std::span<const ClangParseDiagnostic> diagnostics);
} // namespace mockfakegen
