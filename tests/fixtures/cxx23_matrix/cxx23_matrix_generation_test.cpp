#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../GoldenDiff.h"
#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"
#include "generator/CodeGenerator.h"
#include "generator/GeneratedFormatter.h"

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

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_cxx23_matrix_test_" + std::to_string(UniqueSuffix())))
		{
			std::filesystem::remove_all(root_);
			std::filesystem::create_directories(root_);
		}

		TempTree(const TempTree&) = delete;
		TempTree& operator=(const TempTree&) = delete;

		~TempTree()
		{
			std::error_code error;
			std::filesystem::remove_all(root_, error);
		}

		[[nodiscard]] const std::filesystem::path& root() const noexcept
		{
			return root_;
		}

		void Write(std::string_view relative_path, std::string_view content) const
		{
			const auto path = root_ / std::filesystem::path(relative_path);
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream << content;
			Expect(stream.good(), "test header should be written");
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
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

	[[nodiscard]] mockfakegen::ClassExtractionResult
	ExtractHeader(const std::filesystem::path& root, std::string_view header_name)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = root / std::filesystem::path(header_name),
			.project_root = root,
		});
		Expect(parse_result.success, "C++23 matrix fixture should parse");
		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = std::filesystem::path(header_name),
			.include_spelling = std::string(header_name),
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		return mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
	}

	[[nodiscard]] bool HasTopLevelUnsupportedKind(const mockfakegen::ClassExtractionResult& result,
												  std::string_view kind)
	{
		for (const auto& unsupported : result.unsupported_items)
		{
			if (unsupported.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool HasUnsupportedKind(const mockfakegen::ClassModel& class_model,
										  std::string_view kind)
	{
		for (const auto& unsupported : class_model.unsupported_items)
		{
			if (unsupported.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	void GeneratesSupportedModernDeclarations()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/cxx23_matrix/product";
		const auto generated_dir = source_dir / "tests/fixtures/cxx23_matrix/generated";
		const auto extraction = ExtractHeader(product_dir, "Supported.h");

		Expect(extraction.classes.size() == 1U, "supported C++23 matrix class should extract");
		Expect(extraction.unsupported_items.empty(),
			   "supported C++23 matrix should not produce top-level unsupported items");
		const auto& class_model = extraction.classes[0];
		Expect(class_model.qualified_name == "app::v1::Supported",
			   "nested namespace should be preserved");
		Expect(class_model.namespaces.size() == 2U && class_model.namespaces[0] == "app" &&
				   class_model.namespaces[1] == "v1",
			   "nested namespace parts should be preserved");
		Expect(
			class_model.mock_methods.size() == 4U,
			"using alias, trailing return, typedef, enum class, and static method should generate");
		Expect(class_model.unsupported_items.empty(),
			   "friend declarations should be ignored without unsupported diagnostics");
		Expect(class_model.mock_methods[0].return_type_spelling == "Count",
			   "using alias return spelling should be preserved");
		Expect(class_model.mock_methods[0].parameters[0].type_spelling == "Mode",
			   "enum class parameter spelling should be preserved");
		Expect(class_model.mock_methods[0].is_const && class_model.mock_methods[0].is_noexcept,
			   "const noexcept qualifiers should be preserved");
		Expect(class_model.mock_methods[1].return_type_spelling == "Count",
			   "trailing return type should use resolved AST spelling");
		Expect(class_model.mock_methods[1].is_const,
			   "trailing return method const qualifier should be preserved");
		Expect(class_model.mock_methods[2].return_type_spelling == "Ratio",
			   "typedef return spelling should be preserved");
		Expect(class_model.mock_methods[3].is_static, "static method should be preserved");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		const auto format_result = mockfakegen::FormatGeneratedFiles(
			mockfakegen::GeneratedFormatOptions{
				.style = mockfakegen::FormatStyleKind::File,
				.style_search_root = source_dir,
			},
			generated);
		Expect(format_result.ok(), "supported C++23 matrix generated files should format");

		for (const auto& file : format_result.files)
		{
			Expect(!Contains(file.content, "ket::"), "generated file should not contain ket::");
			Expect(!Contains(file.content, "ket_"), "generated file should not contain ket_");
			ExpectGolden(generated_dir / file.relative_path, file.content);
		}
	}

	void RecordsAnonymousNamespaceAsUnsupported()
	{
		TempTree tree;
		tree.Write("include/Anonymous.h",
				   "#pragma once\n"
				   "namespace {\n"
				   "class Hidden {\n"
				   "public:\n"
				   "  bool Run();\n"
				   "};\n"
				   "}\n");

		const auto extraction = ExtractHeader(tree.root(), "include/Anonymous.h");

		Expect(extraction.classes.empty(),
			   "anonymous namespace class should not become a generated class");
		Expect(HasTopLevelUnsupportedKind(extraction, "anonymous_namespace"),
			   "anonymous namespace class should be explicitly unsupported");
	}

	void RecordsExplicitObjectParameterWhenClangSupportsIt()
	{
		TempTree tree;
		tree.Write("include/ExplicitObject.h",
				   "#pragma once\n"
				   "class ExplicitObject {\n"
				   "public:\n"
				   "  bool Self(this ExplicitObject& self, int value);\n"
				   "};\n");

		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / "include/ExplicitObject.h",
			.project_root = tree.root(),
		});
		if (!parse_result.success || parse_result.ast == nullptr)
		{
			return;
		}

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = "include/ExplicitObject.h",
			.include_spelling = "include/ExplicitObject.h",
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U,
			   "explicit object parameter fixture should extract class when Clang supports syntax");
		Expect(extraction.classes[0].mock_methods.empty(),
			   "explicit object parameter should not be generated silently");
		Expect(HasUnsupportedKind(extraction.classes[0], "explicit_object_parameter"),
			   "explicit object parameter should have stable unsupported kind");
	}
} // namespace

int main()
{
	GeneratesSupportedModernDeclarations();
	RecordsAnonymousNamespaceAsUnsupported();
	RecordsExplicitObjectParameterWhenClangSupportsIt();
	return 0;
}
