#pragma once

#include <span>
#include <string>
#include <vector>

#include "model/GeneratedFile.h"
#include "model/ProjectModel.h"

namespace mockfakegen
{
	struct SimpleParameterModel
	{
		std::string type;
		std::string gmock_type = {};
		std::string name;
	};

	struct SimpleMethodModel
	{
		std::string return_type;
		std::string gmock_return_type = {};
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
		std::string mock_header_name;
		std::string fake_source_name;
		std::vector<SimpleMethodModel> methods;
		bool link_ready = true;
		std::vector<std::string> link_readiness_reasons = {};
	};

	struct ProjectGenerationOptions
	{
		bool emit_all_mocks = true;
		bool emit_cmake_fragment = true;
		bool emit_manifest = true;
		bool emit_report = true;
	};

	[[nodiscard]] std::vector<GeneratedFile>
	GenerateMinimalMockFake(const SimpleClassModel& class_model);
	[[nodiscard]] std::vector<GeneratedFile> GenerateMinimalMockFake(const ClassModel& class_model);
	[[nodiscard]] GeneratedFile GenerateAllMocksHeader(std::span<const GeneratedFile> files);
	[[nodiscard]] GeneratedFile GenerateCMakeFragment(std::span<const GeneratedFile> files);
	[[nodiscard]] GeneratedFile GenerateManifestJson(std::span<const ClassModel> class_models);
	[[nodiscard]] GeneratedFile GenerateGenerationReport(std::span<const ClassModel> class_models);
	[[nodiscard]] std::vector<GeneratedFile>
	GenerateMockFakeProject(std::span<const ClassModel> class_models,
							ProjectGenerationOptions options = {});
} // namespace mockfakegen
