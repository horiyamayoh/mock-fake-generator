#pragma once

#include <cstddef>
#include <optional>
#include <string>

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
		std::size_t filtered_class_count = 0U;
	};

	struct ClassExtractionOptions
	{
		bool fake_special_members = false;
		bool fake_static_data = false;
		bool interface_mock = false;
		std::optional<std::string> class_filter = std::nullopt;
	};

	[[nodiscard]] ClassExtractionResult
	ExtractClassDefinitionsFromAst(const clang::ASTUnit& ast,
								   const HeaderModel& target_header,
								   ClassExtractionOptions options = {});
} // namespace mockfakegen
