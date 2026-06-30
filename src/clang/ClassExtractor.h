#pragma once

#include "model/ProjectModel.h"

namespace clang
{
	class ASTUnit;
} // namespace clang

namespace mockfakegen
{
	struct ClassExtractionResult
	{
		std::vector<ClassModel> classes;
		std::vector<UnsupportedItem> unsupported_items;
		std::vector<Diagnostic> diagnostics;
	};

	[[nodiscard]] ClassExtractionResult
	ExtractClassDefinitionsFromAst(const clang::ASTUnit& ast, const HeaderModel& target_header);
} // namespace mockfakegen
