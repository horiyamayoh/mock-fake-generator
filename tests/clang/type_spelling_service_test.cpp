#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"
#include "clang/TypeSpellingService.h"

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
					("mockfakegen_type_spelling_service_test_" + std::to_string(UniqueSuffix())))
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

	[[nodiscard]] mockfakegen::ClassModel ExtractOnlyClass(const TempTree& tree,
														   std::string_view relative_header)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / std::filesystem::path(relative_header),
			.project_root = tree.root(),
		});
		Expect(parse_result.success, "type spelling fixture should parse");
		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		auto result = mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(result.classes.size() == 1U, "type spelling fixture should extract one class");
		return result.classes[0];
	}

	[[nodiscard]] const mockfakegen::MethodModel&
	FindMethod(const mockfakegen::ClassModel& class_model, std::string_view name)
	{
		for (const auto& method : class_model.mock_methods)
		{
			if (method.name == name)
			{
				return method;
			}
		}
		std::cerr << "missing method: " << name << '\n';
		std::exit(1);
	}

	void SpellsBasicPointerReferenceAndStringTypes()
	{
		TempTree tree;
		tree.Write("include/Types.h",
				   "#pragma once\n"
				   "#include <string>\n"
				   "class Types {\n"
				   "public:\n"
				   "  void VoidReturn();\n"
				   "  bool BoolReturn();\n"
				   "  int* Pointer(int& ref, const std::string& name);\n"
				   "};\n");

		const auto class_model = ExtractOnlyClass(tree, "include/Types.h");
		const auto& void_method = FindMethod(class_model, "VoidReturn");
		Expect(void_method.return_type_spelling == "void", "void return should be spelled");
		const auto& bool_method = FindMethod(class_model, "BoolReturn");
		Expect(bool_method.return_type_spelling == "bool", "bool return should be spelled");
		const auto& pointer_method = FindMethod(class_model, "Pointer");
		Expect(pointer_method.return_type_spelling == "int*",
			   "pointer return should normalize spacing");
		Expect(pointer_method.parameters[0].type_spelling == "int&",
			   "reference parameter should normalize spacing");
		Expect(pointer_method.parameters[1].type_spelling == "const std::string&",
			   "std::string reference should be spelled");
	}

	void NormalizesArrayParametersAndSynthesizesNames()
	{
		TempTree tree;
		tree.Write("include/Arrays.h",
				   "#pragma once\n"
				   "class Arrays {\n"
				   "public:\n"
				   "  bool Initialize(int, char* argv[]);\n"
				   "};\n");

		const auto class_model = ExtractOnlyClass(tree, "include/Arrays.h");
		const auto& method = FindMethod(class_model, "Initialize");
		Expect(method.parameters[0].type_spelling == "int", "first parameter type should be int");
		Expect(method.parameters[0].generated_name == "arg0",
			   "missing first parameter name should synthesize arg0");
		Expect(method.parameters[1].type_spelling == "char**",
			   "array parameter should decay to pointer spelling");
		Expect(method.parameters[1].gmock_type_spelling == "char**",
			   "gMock array parameter spelling should match pointer spelling");
		Expect(method.parameters[1].generated_name == "argv",
			   "present parameter name should be preserved");
	}

	void WrapsCommaTypesForGMock()
	{
		TempTree tree;
		tree.Write("include/Pairs.h",
				   "#pragma once\n"
				   "#include <utility>\n"
				   "class Pairs {\n"
				   "public:\n"
				   "  std::pair<int, int> GetPair();\n"
				   "  void SetPair(std::pair<int, int> value);\n"
				   "};\n");

		const auto class_model = ExtractOnlyClass(tree, "include/Pairs.h");
		const auto& get_pair = FindMethod(class_model, "GetPair");
		Expect(get_pair.return_type_spelling == "std::pair<int, int>",
			   "pair return type should be spelled");
		Expect(get_pair.gmock_return_type_spelling == "(std::pair<int, int>)",
			   "pair return type should be wrapped for gMock");
		const auto& set_pair = FindMethod(class_model, "SetPair");
		Expect(set_pair.parameters[0].type_spelling == "std::pair<int, int>",
			   "pair parameter type should be spelled");
		Expect(set_pair.parameters[0].gmock_type_spelling == "(std::pair<int, int>)",
			   "pair parameter type should be wrapped for gMock");
	}

	void WrapDecisionIgnoresCommasInsideParentheses()
	{
		Expect(mockfakegen::TypeSpellingService::NeedsGMockParens("std::pair<int, int>"),
			   "template comma should need gMock wrapping");
		Expect(mockfakegen::TypeSpellingService::NeedsGMockParens(
				   "std::map<int, std::pair<int, double>>"),
			   "nested template comma should need gMock wrapping");
		Expect(!mockfakegen::TypeSpellingService::NeedsGMockParens("void (*)(int, int)"),
			   "function pointer parameter comma is already parenthesized");
	}
} // namespace

int main()
{
	SpellsBasicPointerReferenceAndStringTypes();
	NormalizesArrayParametersAndSynthesizesNames();
	WrapsCommaTypesForGMock();
	WrapDecisionIgnoresCommasInsideParentheses();
	return 0;
}
