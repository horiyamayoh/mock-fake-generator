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
		Expect(method.parameters[1].declaration_spelling == "char** argv",
			   "array parameter declaration should preserve adjusted pointer spelling");
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

	void SpellsFunctionPointerAndReferenceParameterDeclarators()
	{
		TempTree tree;
		tree.Write("include/Callbacks.h",
				   "#pragma once\n"
				   "class Callbacks {\n"
				   "public:\n"
				   "  void Register(void (*callback)(int, int), int (&values)[3]);\n"
				   "};\n");

		const auto class_model = ExtractOnlyClass(tree, "include/Callbacks.h");
		const auto& method = FindMethod(class_model, "Register");
		Expect(method.parameters[0].type_spelling == "void (*)(int, int)",
			   "function pointer parameter type should be spelled without a name");
		Expect(
			method.parameters[0].declaration_spelling == "void (*callback)(int, int)",
			"function pointer parameter declaration should place the name inside the declarator");
		Expect(method.parameters[1].type_spelling == "int (&)[3]",
			   "array reference parameter type should be spelled without a name");
		Expect(method.parameters[1].declaration_spelling == "int (&values)[3]",
			   "array reference declaration should place the name inside the declarator");
	}

	void QualifiesPublicNestedTypesAndRejectsPrivateNestedTypes()
	{
		TempTree tree;
		tree.Write("include/NestedTypes.h",
				   "#pragma once\n"
				   "#include <vector>\n"
				   "class PublicNested {\n"
				   "public:\n"
				   "  struct Token {};\n"
				   "  using Alias = int;\n"
				   "  Token Make(Token token);\n"
				   "  std::vector<Token> Items();\n"
				   "  void Put(std::vector<Token> tokens);\n"
				   "  Alias GetAlias();\n"
				   "  void SetAlias(Alias value);\n"
				   "  std::vector<Alias> AliasItems();\n"
				   "};\n"
				   "class PrivateNested {\n"
				   "  class Hidden {};\n"
				   "public:\n"
				   "  Hidden Make(Hidden value);\n"
				   "};\n");

		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / "include/NestedTypes.h",
			.project_root = tree.root(),
		});
		Expect(parse_result.success, "nested type fixture should parse");
		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto result = mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(result.classes.size() == 2U, "outer classes should be extracted");
		const auto& public_nested = result.classes[0].qualified_name == "PublicNested"
			? result.classes[0]
			: result.classes[1];
		const auto& private_nested = result.classes[0].qualified_name == "PrivateNested"
			? result.classes[0]
			: result.classes[1];

		const auto& method = FindMethod(public_nested, "Make");
		Expect(method.return_type_spelling == "PublicNested::Token",
			   "public nested return type should be qualified for generated namespace context");
		Expect(method.parameters[0].type_spelling == "PublicNested::Token",
			   "public nested parameter type should be qualified");
		Expect(method.parameters[0].declaration_spelling == "PublicNested::Token token",
			   "public nested parameter declaration should stay qualified");
		const auto& items = FindMethod(public_nested, "Items");
		Expect(items.return_type_spelling == "std::vector<PublicNested::Token>",
			   "public nested template argument return type should be qualified");
		Expect(items.gmock_return_type_spelling == "std::vector<PublicNested::Token>",
			   "public nested template argument gMock return type should be qualified");
		const auto& put = FindMethod(public_nested, "Put");
		Expect(put.parameters[0].type_spelling == "std::vector<PublicNested::Token>",
			   "public nested template argument parameter type should be qualified");
		Expect(put.parameters[0].declaration_spelling == "std::vector<PublicNested::Token> tokens",
			   "public nested template argument declaration should stay qualified");
		const auto& get_alias = FindMethod(public_nested, "GetAlias");
		Expect(get_alias.return_type_spelling == "PublicNested::Alias",
			   "public nested alias return type should be qualified");
		const auto& set_alias = FindMethod(public_nested, "SetAlias");
		Expect(set_alias.parameters[0].type_spelling == "PublicNested::Alias",
			   "public nested alias parameter type should be qualified");
		Expect(set_alias.parameters[0].declaration_spelling == "PublicNested::Alias value",
			   "public nested alias declaration should stay qualified");
		const auto& alias_items = FindMethod(public_nested, "AliasItems");
		Expect(alias_items.return_type_spelling == "std::vector<PublicNested::Alias>",
			   "public nested alias template argument should be qualified");
		Expect(private_nested.mock_methods.empty(),
			   "method using private nested type should not be generated");
		Expect(HasUnsupportedKind(private_nested, "private_nested_type"),
			   "private nested type should be reported unsupported");
		Expect(HasTopLevelUnsupportedKind(result, "nested_class"),
			   "nested class definition should be reported unsupported");
	}
} // namespace

int main()
{
	SpellsBasicPointerReferenceAndStringTypes();
	NormalizesArrayParametersAndSynthesizesNames();
	WrapsCommaTypesForGMock();
	WrapDecisionIgnoresCommasInsideParentheses();
	SpellsFunctionPointerAndReferenceParameterDeclarators();
	QualifiesPublicNestedTypesAndRejectsPrivateNestedTypes();
	return 0;
}
