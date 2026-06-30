#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"

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
					("mockfakegen_class_extractor_test_" + std::to_string(UniqueSuffix())))
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

	[[nodiscard]] mockfakegen::ClassExtractionResult
	ParseAndExtract(const TempTree& tree, std::string_view relative_header)
	{
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / std::filesystem::path(relative_header),
			.project_root = tree.root(),
		});
		Expect(parse_result.success, "fixture header should parse");
		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = parse_result.header.parsed_by_synthetic_tu,
		};
		return mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
	}

	void ExtractsGlobalClass()
	{
		TempTree tree;
		tree.Write("include/Hoge.h", "#pragma once\nclass Hoge { public: bool DoSomething(); };\n");

		const auto result = ParseAndExtract(tree, "include/Hoge.h");

		Expect(result.classes.size() == 1U, "global class should be extracted");
		Expect(result.classes[0].name == "Hoge", "class name should be extracted");
		Expect(result.classes[0].qualified_name == "Hoge",
			   "global qualified name should be extracted");
		Expect(result.classes[0].namespaces.empty(), "global class should have no namespace parts");
		Expect(result.classes[0].mock_name == "MockHoge", "mock name should be initialized");
		Expect(result.classes[0].source_header.include_spelling == "include/Hoge.h",
			   "source header should be attached");
	}

	void ExtractsNamespacedClass()
	{
		TempTree tree;
		tree.Write("include/app/Hoge.h",
				   "#pragma once\nnamespace app { class Hoge { public: bool DoSomething(); }; }\n");

		const auto result = ParseAndExtract(tree, "include/app/Hoge.h");

		Expect(result.classes.size() == 1U, "namespaced class should be extracted");
		Expect(result.classes[0].name == "Hoge", "namespaced class short name should be extracted");
		Expect(result.classes[0].qualified_name == "app::Hoge",
			   "namespaced class qualified name should be extracted");
		Expect(result.classes[0].namespaces.size() == 1U &&
				   result.classes[0].namespaces[0] == "app",
			   "namespace parts should be extracted");
	}

	void SkipsForwardDeclarationAnonymousStructAndSystemHeaders()
	{
		TempTree tree;
		tree.Write("include/Mixed.h",
				   "#pragma once\n"
				   "#include <string>\n"
				   "class Forward;\n"
				   "struct StructOptInLater {};\n"
				   "class { public: int value; } anonymous_instance;\n"
				   "class Defined {};\n");

		const auto result = ParseAndExtract(tree, "include/Mixed.h");

		Expect(result.classes.size() == 1U,
			   "only concrete named class definitions from target header should be extracted");
		Expect(result.classes[0].qualified_name == "Defined", "defined class should be extracted");
	}

	void RecordsClassTemplateAsUnsupported()
	{
		TempTree tree;
		tree.Write("include/Templates.h",
				   "#pragma once\n"
				   "template <class T> class Box { public: T value; };\n"
				   "class Hoge {};\n");

		const auto result = ParseAndExtract(tree, "include/Templates.h");

		Expect(result.classes.size() == 1U, "class template should not become generated class");
		Expect(result.classes[0].qualified_name == "Hoge",
			   "non-template class should be extracted");
		Expect(result.unsupported_items.size() == 1U,
			   "class template should be recorded unsupported");
		Expect(result.unsupported_items[0].kind == "class_template",
			   "unsupported kind should identify class template");
		Expect(result.unsupported_items[0].name == "Box",
			   "unsupported item should record template name");
		Expect(!result.unsupported_items[0].reason.empty(),
			   "unsupported item should explain reason");
	}

	[[nodiscard]] bool HasUnsupportedKind(const mockfakegen::ClassModel& class_model,
										  std::string_view kind)
	{
		for (const auto& item : class_model.unsupported_items)
		{
			if (item.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	void ExtractsPublicMethodsAndQualifiersInDeclarationOrder()
	{
		TempTree tree;
		tree.Write("include/Methods.h",
				   "#pragma once\n"
				   "class Methods {\n"
				   "public:\n"
				   "  bool Initialize(int argc);\n"
				   "  static int Count();\n"
				   "  bool Check(double value = 1.0) const & noexcept;\n"
				   "  void MoveOnly() &&;\n"
				   "  bool Overload(int value);\n"
				   "  bool Overload(double value);\n"
				   "private:\n"
				   "  void Hidden();\n"
				   "protected:\n"
				   "  void ProtectedHook();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/Methods.h");

		Expect(result.classes.size() == 1U, "method fixture class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 6U, "six public methods should be generated");
		Expect(class_model.fake_methods.size() == 6U, "fake methods should mirror public methods");
		Expect(class_model.mock_methods[0].name == "Initialize",
			   "declaration order should be kept");
		Expect(class_model.mock_methods[1].name == "Count", "static method should keep order");
		Expect(class_model.mock_methods[1].is_static, "static method should be flagged");
		Expect(class_model.mock_methods[2].name == "Check", "qualified method should keep order");
		Expect(class_model.mock_methods[2].is_const, "const method should be flagged");
		Expect(class_model.mock_methods[2].is_noexcept, "simple noexcept should be flagged");
		Expect(class_model.mock_methods[2].ref_qualifier == mockfakegen::RefQualifierKind::LValue,
			   "lvalue ref qualifier should be flagged");
		Expect(class_model.mock_methods[2].parameters.size() == 1U,
			   "default-argument parameter should be extracted");
		Expect(class_model.mock_methods[2].parameters[0].has_default_argument,
			   "default argument presence should be kept");
		Expect(class_model.mock_methods[3].ref_qualifier == mockfakegen::RefQualifierKind::RValue,
			   "rvalue ref qualifier should be flagged");
		Expect(class_model.mock_methods[4].name == "Overload" &&
				   class_model.mock_methods[5].name == "Overload",
			   "overloads should be kept as separate methods");
		Expect(HasUnsupportedKind(class_model, "non_public_method"),
			   "private/protected methods should be recorded as unsupported");
	}

	void RecordsUnsupportedMethodConstructs()
	{
		TempTree tree;
		tree.Write("include/UnsupportedMethods.h",
				   "#pragma once\n"
				   "class UnsupportedMethods {\n"
				   "public:\n"
				   "  UnsupportedMethods();\n"
				   "  ~UnsupportedMethods();\n"
				   "  operator bool() const;\n"
				   "  UnsupportedMethods& operator=(const UnsupportedMethods& other);\n"
				   "  template <class T> T Get();\n"
				   "  void Deleted() = delete;\n"
				   "  void Inline() {}\n"
				   "  constexpr int Value() const { return 0; }\n"
				   "  void Conditional() noexcept(sizeof(int) == 4);\n"
				   "  void Volatile() volatile;\n"
				   "  bool Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/UnsupportedMethods.h");

		Expect(result.classes.size() == 1U, "unsupported method fixture class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 1U, "only supported method should be generated");
		Expect(class_model.mock_methods[0].name == "Supported",
			   "supported method should be retained");
		Expect(HasUnsupportedKind(class_model, "constructor"), "constructor should be unsupported");
		Expect(HasUnsupportedKind(class_model, "destructor"), "destructor should be unsupported");
		Expect(HasUnsupportedKind(class_model, "conversion_operator"),
			   "conversion operator should be unsupported");
		Expect(HasUnsupportedKind(class_model, "overloaded_operator"),
			   "overloaded operator should be unsupported");
		Expect(HasUnsupportedKind(class_model, "function_template"),
			   "function template should be unsupported");
		Expect(HasUnsupportedKind(class_model, "deleted_method"),
			   "deleted method should be unsupported");
		Expect(HasUnsupportedKind(class_model, "inline_body"), "inline body should be unsupported");
		Expect(HasUnsupportedKind(class_model, "constexpr_method"),
			   "constexpr method should be unsupported");
		Expect(HasUnsupportedKind(class_model, "conditional_noexcept"),
			   "conditional noexcept should be unsupported");
		Expect(HasUnsupportedKind(class_model, "volatile_method"),
			   "volatile method should be unsupported");
		Expect(!result.diagnostics.empty(), "unsupported methods should emit diagnostics");
		Expect(result.diagnostics[0].code == mockfakegen::DiagnosticCode::UnsupportedConstruct,
			   "unsupported diagnostics should be distinct from parse diagnostics");
	}
} // namespace

int main()
{
	ExtractsGlobalClass();
	ExtractsNamespacedClass();
	SkipsForwardDeclarationAnonymousStructAndSystemHeaders();
	RecordsClassTemplateAsUnsupported();
	ExtractsPublicMethodsAndQualifiersInDeclarationOrder();
	RecordsUnsupportedMethodConstructs();
	return 0;
}
