#pragma once

#include <string>
#include <vector>

#include "model/GeneratedFile.h"
#include "model/ProjectModel.h"

namespace mockfakegen
{
	struct SimpleParameterModel
	{
		std::string type;
		std::string name;
	};

	struct SimpleMethodModel
	{
		std::string return_type;
		std::string name;
		std::vector<SimpleParameterModel> parameters;
		bool is_const = false;
		bool is_noexcept = false;
		RefQualifierKind ref_qualifier = RefQualifierKind::None;
	};

	struct SimpleClassModel
	{
		std::string name;
		std::vector<std::string> namespaces;
		std::string header_include;
		std::vector<SimpleMethodModel> methods;
	};

	[[nodiscard]] std::vector<GeneratedFile>
	GenerateMinimalMockFake(const SimpleClassModel& class_model);
	[[nodiscard]] std::vector<GeneratedFile> GenerateMinimalMockFake(const ClassModel& class_model);
} // namespace mockfakegen
