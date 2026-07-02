#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../GoldenDiff.h"
#include "Config.h"
#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"
#include "diagnostics/RunDiagnostic.h"
#include "generator/CodeGenerator.h"
#include "model/GeneratedFile.h"
#include "validation/GeneratedCompileValidator.h"
#include "validation/GenerationPolicy.h"

#ifndef MOCKFAKEGEN_CXX_COMPILER
#define MOCKFAKEGEN_CXX_COMPILER "c++"
#endif

#ifndef MOCKFAKEGEN_GMOCK_INCLUDE_DIRS
#define MOCKFAKEGEN_GMOCK_INCLUDE_DIRS ""
#endif

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	void WriteText(const std::filesystem::path& path, std::string_view content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << content;
		stream.close();
		Expect(stream.good(), "fixture file should be writable");
	}

	[[nodiscard]] bool UpdateGoldens()
	{
		const auto* const value = std::getenv("MOCKFAKEGEN_UPDATE_GOLDENS");
		return value != nullptr && std::string_view(value) == "1";
	}

	void ExpectGolden(const std::filesystem::path& path, std::string_view actual)
	{
		if (UpdateGoldens())
		{
			WriteText(path, actual);
			return;
		}

		const auto expected = ReadText(path);
		mockfakegen_fixture::ExpectGoldenTextEqual(actual, expected, path);
	}

	[[nodiscard]] std::vector<std::filesystem::path> SplitPathList(std::string_view text)
	{
		std::vector<std::filesystem::path> paths;
		std::size_t offset = 0U;
		while (offset <= text.size())
		{
			const auto separator = text.find('|', offset);
			const auto part = text.substr(offset, separator - offset);
			if (!part.empty())
			{
				paths.emplace_back(part);
			}
			if (separator == std::string_view::npos)
			{
				break;
			}
			offset = separator + 1U;
		}
		return paths;
	}

	void ExpectSyntaxValidationPasses(std::span<const mockfakegen::GeneratedFile> generated,
									  const std::filesystem::path& product_dir)
	{
		auto include_dirs = SplitPathList(MOCKFAKEGEN_GMOCK_INCLUDE_DIRS);
		include_dirs.push_back(product_dir);

		const auto validation = mockfakegen::ValidateGeneratedOutputCompile(
			mockfakegen::GeneratedCompileValidationOptions{
				.mode = mockfakegen::ValidationMode::Syntax,
				.compiler = std::filesystem::path(MOCKFAKEGEN_CXX_COMPILER),
				.include_dirs = include_dirs,
				.link_files = {},
				.extra_args = {"-D_Nonnull="},
				.source_args = {},
				.command_timeout = std::chrono::seconds(30),
				.keep_failed_artifacts = true,
				.artifact_dir = {},
			},
			generated);

		if (!validation.ok())
		{
			for (const auto& diagnostic : validation.diagnostics)
			{
				std::cerr << diagnostic.source_path.generic_string() << ": " << diagnostic.message
						  << '\n'
						  << diagnostic.stderr_summary << '\n';
			}
		}
		Expect(validation.ok(), "unsupported diagnostic generated files should pass syntax");
	}

	[[nodiscard]] mockfakegen::ClassExtractionResult
	ExtractHeader(const std::filesystem::path& product_dir,
				  std::string_view header_name,
				  mockfakegen::ClassExtractionOptions options)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / header_name,
			.project_root = product_dir,
		});
		Expect(parse_result.success, "unsupported diagnostics fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = std::filesystem::path(header_name),
			.include_spelling = std::string(header_name),
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header, options);
		for (auto& item : extraction.unsupported_items)
		{
			item.source_range.begin.file = std::filesystem::path(header_name);
			item.source_range.end.file = std::filesystem::path(header_name);
		}
		return extraction;
	}

	[[nodiscard]] std::vector<mockfakegen::RunDiagnostic>
	UnsupportedDiagnostics(const mockfakegen::ClassExtractionResult& extraction)
	{
		auto diagnostics = mockfakegen::BuildUnsupportedItemDiagnostics(extraction.classes);
		auto top_level = mockfakegen::BuildUnsupportedItemDiagnostics(extraction.unsupported_items);
		diagnostics.insert(diagnostics.end(), top_level.begin(), top_level.end());
		mockfakegen::SortRunDiagnostics(diagnostics);
		return diagnostics;
	}

	[[nodiscard]] mockfakegen::GeneratedFile
	GenerateManifest(const mockfakegen::ClassExtractionResult& extraction)
	{
		mockfakegen::GenerationReportMetadata metadata;
		metadata.diagnostics = UnsupportedDiagnostics(extraction);
		metadata.unsupported_items = extraction.unsupported_items;
		return mockfakegen::GenerateManifestJson(extraction.classes, metadata);
	}

	[[nodiscard]] mockfakegen::GeneratedFile
	GenerateReport(const mockfakegen::ClassExtractionResult& extraction)
	{
		mockfakegen::GenerationReportMetadata metadata;
		metadata.diagnostics = UnsupportedDiagnostics(extraction);
		metadata.unsupported_items = extraction.unsupported_items;
		return mockfakegen::GenerateGenerationReport(extraction.classes, metadata);
	}

	[[nodiscard]] const mockfakegen::GeneratedFile&
	FindGeneratedFile(const std::vector<mockfakegen::GeneratedFile>& files,
					  std::string_view relative_path)
	{
		const auto found =
			std::find_if(files.begin(),
						 files.end(),
						 [relative_path](const auto& file)
						 {
							 return file.relative_path.generic_string() == relative_path;
						 });
		Expect(found != files.end(), "generated file should exist");
		return *found;
	}

	[[nodiscard]] bool HasGeneratedFile(const std::vector<mockfakegen::GeneratedFile>& files,
										std::string_view relative_path)
	{
		return std::any_of(files.begin(),
						   files.end(),
						   [relative_path](const auto& file)
						   {
							   return file.relative_path.generic_string() == relative_path;
						   });
	}

	[[nodiscard]] bool HasUnsupportedKind(const mockfakegen::ClassExtractionResult& extraction,
										  std::string_view kind)
	{
		const auto class_has_kind =
			std::any_of(extraction.classes.begin(),
						extraction.classes.end(),
						[kind](const auto& class_model)
						{
							return std::any_of(class_model.unsupported_items.begin(),
											   class_model.unsupported_items.end(),
											   [kind](const auto& item)
											   {
												   return item.kind == kind;
											   });
						});
		if (class_has_kind)
		{
			return true;
		}

		return std::any_of(extraction.unsupported_items.begin(),
						   extraction.unsupported_items.end(),
						   [kind](const auto& item)
						   {
							   return item.kind == kind;
						   });
	}

	[[nodiscard]] bool HasUnsupportedMember(const mockfakegen::ClassExtractionResult& extraction,
											std::string_view kind,
											std::string_view member)
	{
		return std::any_of(extraction.classes.begin(),
						   extraction.classes.end(),
						   [kind, member](const auto& class_model)
						   {
							   return std::any_of(class_model.unsupported_items.begin(),
												  class_model.unsupported_items.end(),
												  [kind, member](const auto& item)
												  {
													  return item.kind == kind &&
														  Contains(item.member_signature, member);
												  });
						   });
	}

	[[nodiscard]] std::size_t
	UnsupportedItemCount(const mockfakegen::ClassExtractionResult& extraction)
	{
		std::size_t count = extraction.unsupported_items.size();
		for (const auto& class_model : extraction.classes)
		{
			count += class_model.unsupported_items.size();
		}
		return count;
	}

	void ExpectPolicyBehavior(const mockfakegen::ClassExtractionResult& extraction)
	{
		const std::vector<mockfakegen::Diagnostic> parse_diagnostics;
		const std::vector<mockfakegen::GeneratedCompileDiagnostic> validation_diagnostics;

		mockfakegen::Config best_effort_config;
		best_effort_config.strict = false;
		best_effort_config.best_effort = true;
		const auto best_effort = mockfakegen::EvaluateGenerationPolicy(
			best_effort_config,
			mockfakegen::GenerationPolicyInput{
				.classes = extraction.classes,
				.unsupported_items = extraction.unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});
		Expect(best_effort.exit_code == 0, "best-effort unsupported fixture should succeed");
		Expect(best_effort.publish_generated_files,
			   "best-effort unsupported fixture should publish generated files");
		Expect(best_effort.emit_manifest, "best-effort unsupported fixture should emit manifest");
		Expect(best_effort.emit_report, "best-effort unsupported fixture should emit report");

		mockfakegen::Config strict_config;
		strict_config.strict = true;
		strict_config.best_effort = false;
		const auto strict = mockfakegen::EvaluateGenerationPolicy(
			strict_config,
			mockfakegen::GenerationPolicyInput{
				.classes = extraction.classes,
				.unsupported_items = extraction.unsupported_items,
				.parse_diagnostics = parse_diagnostics,
				.validation_diagnostics = validation_diagnostics,
			});
		Expect(strict.exit_code != 0, "strict unsupported fixture should fail");
		Expect(strict.publish_generated_files,
			   "strict unsupported fixture should still publish diagnostic generated files");
		Expect(strict.emit_manifest, "strict unsupported fixture should emit manifest");
		Expect(strict.emit_report, "strict unsupported fixture should emit report");
	}

	void GeneratesLinkReplacementUnsupportedGoldens()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/unsupported_diagnostics/product";
		const auto generated_dir =
			source_dir / "tests/fixtures/unsupported_diagnostics/generated/link_replacement";
		const auto extraction = ExtractHeader(product_dir,
											  "Unsupported.h",
											  mockfakegen::ClassExtractionOptions{
												  .fake_special_members = true,
												  .fake_static_data = true,
											  });

		Expect(extraction.classes.size() == 3U,
			   "link replacement negative fixture should extract three concrete classes");
		Expect(UnsupportedItemCount(extraction) == 33U,
			   "link replacement negative fixture should lock unsupported item count");
		for (const auto kind : {
				 "class_template",
				 "class_template_specialization",
				 "class_template_partial_specialization",
				 "nested_class",
				 "function_template",
				 "inline_body",
				 "auto_return",
				 "decltype_auto_return",
				 "constexpr_method",
				 "consteval_method",
				 "attributed_type",
				 "conversion_operator",
				 "defaulted_method",
				 "overloaded_operator",
				 "pure_virtual_method",
				 "volatile_method",
				 "macro_origin",
				 "non_public_method",
				 "private_nested_type",
				 "constructor",
				 "destructor",
				 "static_data_member",
			 })
		{
			Expect(HasUnsupportedKind(extraction, kind), "expected unsupported kind should exist");
		}
		Expect(HasUnsupportedMember(extraction, "inline_body", "InlineBody"),
			   "inline body should be recorded");
		Expect(HasUnsupportedMember(extraction, "inline_body", "OutOfClassInline"),
			   "out-of-class inline definition should be recorded");
		Expect(HasUnsupportedMember(extraction, "auto_return", "AutoValue"),
			   "plain auto return should be recorded");
		Expect(HasUnsupportedMember(extraction, "decltype_auto_return", "Deduced"),
			   "decltype(auto) return should be recorded");
		Expect(HasUnsupportedMember(extraction, "attributed_type", "NonNull"),
			   "platform attributed type should be recorded");
		Expect(HasUnsupportedMember(extraction, "defaulted_method", "operator<=>"),
			   "defaulted comparison should be recorded");
		Expect(HasUnsupportedMember(extraction, "function_template", "Constrained"),
			   "constrained member template should be recorded");
		Expect(HasUnsupportedMember(extraction, "non_public_method", "ProtectedMethod"),
			   "protected method should be recorded");
		Expect(HasUnsupportedMember(extraction, "non_public_method", "PrivateMethod"),
			   "private method should be recorded");
		Expect(HasUnsupportedMember(extraction, "private_nested_type", "HiddenReturn"),
			   "private nested return type should be recorded");
		Expect(HasUnsupportedMember(extraction, "private_nested_type", "HiddenParam"),
			   "private nested parameter type should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "ref_value"),
			   "reference static data should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "values"),
			   "array static data should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "initialized"),
			   "in-class initialized static data should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "boot"),
			   "constinit static data should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "tls_value"),
			   "thread-local static data should be recorded");
		Expect(HasUnsupportedMember(extraction, "static_data_member", "private_token"),
			   "nested-type static data should be recorded");
		ExpectPolicyBehavior(extraction);

		const auto generated =
			mockfakegen::GenerateMockFakeProject(extraction.classes,
												 mockfakegen::ProjectGenerationOptions{
													 .emit_manifest = false,
													 .emit_report = false,
												 });
		Expect(HasGeneratedFile(generated, "FakeUnsupportedSurface.cpp"),
			   "not-link-ready fake should still be generated for diagnostics");
		Expect(HasGeneratedFile(generated, "FakeUnsafeSpecial.cpp"),
			   "not-link-ready special-member fake should still be generated for diagnostics");
		Expect(HasGeneratedFile(generated, "FakeUnsafeStaticData.cpp"),
			   "not-link-ready static-data fake should still be generated for diagnostics");
		ExpectSyntaxValidationPasses(generated, product_dir);

		const auto& cmake_fragment = FindGeneratedFile(generated, "CMakeLists.fragment.cmake");
		Expect(!Contains(cmake_fragment.content, "FakeUnsupportedSurface.cpp"),
			   "not-link-ready surface fake should be excluded from CMake source list");
		Expect(!Contains(cmake_fragment.content, "FakeUnsafeSpecial.cpp"),
			   "not-link-ready special fake should be excluded from CMake source list");
		Expect(!Contains(cmake_fragment.content, "FakeUnsafeStaticData.cpp"),
			   "not-link-ready static-data fake should be excluded from CMake source list");
		for (const auto& file : generated)
		{
			ExpectGolden(generated_dir / file.relative_path, file.content);
		}

		const auto manifest = GenerateManifest(extraction);
		const auto report = GenerateReport(extraction);
		Expect(Contains(manifest.content, "\"source_range\""),
			   "manifest should include unsupported source locations");
		Expect(Contains(manifest.content, "\"link_ready\": false"),
			   "manifest should include link-readiness effect");
		Expect(Contains(report.content, "unsupported items remain"),
			   "report should include link-readiness reason");
		ExpectGolden(generated_dir / "manifest.json", manifest.content);
		ExpectGolden(generated_dir / "generation_report.md", report.content);
	}

	void GeneratesInterfaceUnsupportedGoldens()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/unsupported_diagnostics/product";
		const auto generated_dir =
			source_dir / "tests/fixtures/unsupported_diagnostics/generated/interface";
		const auto extraction = ExtractHeader(product_dir,
											  "Interface.h",
											  mockfakegen::ClassExtractionOptions{
												  .interface_mock = true,
											  });

		Expect(extraction.classes.size() == 1U,
			   "interface negative fixture should extract one interface class");
		Expect(UnsupportedItemCount(extraction) == 3U,
			   "interface negative fixture should lock unsupported item count");
		Expect(HasUnsupportedKind(extraction, "interface_construct"),
			   "interface constructs should be reported unsupported");
		Expect(HasUnsupportedMember(
				   extraction, "interface_construct", "BrokenInterface::BrokenInterface"),
			   "unsupported interface constructor should be recorded");
		Expect(HasUnsupportedMember(extraction, "interface_construct", "~BrokenInterface"),
			   "unsupported interface destructor should be recorded");
		Expect(HasUnsupportedMember(extraction, "non_public_method", "ProtectedPure"),
			   "protected interface method should be recorded");
		ExpectPolicyBehavior(extraction);

		const auto generated =
			mockfakegen::GenerateMockFakeProject(extraction.classes,
												 mockfakegen::ProjectGenerationOptions{
													 .emit_manifest = false,
													 .emit_report = false,
													 .interface_mock = true,
												 });
		Expect(!HasGeneratedFile(generated, "FakeBrokenInterface.cpp"),
			   "interface mock fixture should not generate fake source");
		Expect(!HasGeneratedFile(generated, "CMakeLists.fragment.cmake"),
			   "interface mock fixture should not generate CMake source fragment");

		const auto manifest = GenerateManifest(extraction);
		const auto report = GenerateReport(extraction);
		Expect(Contains(manifest.content, "\"generation_mode\": \"interface-mock\""),
			   "manifest should identify interface mock mode");
		Expect(Contains(manifest.content, "\"link_ready\": false"),
			   "manifest should include interface link-readiness effect");
		ExpectGolden(generated_dir / "manifest.json", manifest.content);
		ExpectGolden(generated_dir / "generation_report.md", report.content);
	}
} // namespace

int main()
{
	GeneratesLinkReplacementUnsupportedGoldens();
	GeneratesInterfaceUnsupportedGoldens();
	return 0;
}
