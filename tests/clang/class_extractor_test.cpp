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
	ParseAndExtract(const TempTree& tree,
					std::string_view relative_header,
					mockfakegen::ClassExtractionOptions options = {})
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
		return mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header, options);
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

	void RecordsTopLevelStructAsUnsupported()
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
		Expect(result.unsupported_items.size() == 1U,
			   "top-level struct should be recorded unsupported");
		Expect(result.unsupported_items[0].kind == "struct_record",
			   "unsupported kind should identify struct");
		Expect(result.unsupported_items[0].name == "StructOptInLater",
			   "unsupported item should record struct name");
		Expect(!result.diagnostics.empty(), "unsupported struct should emit diagnostics");
	}

	void IgnoresDefaultedSpecialMembersWithoutFakeSpecialMembers()
	{
		TempTree tree;
		tree.Write("include/Hoge.h",
				   "#pragma once\n"
				   "class Hoge {\n"
				   "public:\n"
				   "  Hoge() = default;\n"
				   "  ~Hoge() = default;\n"
				   "  bool DoSomething();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/Hoge.h");

		Expect(result.classes.size() == 1U, "class with defaulted special members should extract");
		const auto& class_model = result.classes[0];
		Expect(class_model.qualified_name == "Hoge", "Hoge should be extracted");
		Expect(class_model.unsupported_items.empty(),
			   "defaulted constructor and destructor should not be unsupported");
		Expect(class_model.mock_methods.size() == 1U, "normal method should remain mockable");
		Expect(class_model.fake_methods.size() == 1U, "normal method should remain fakeable");
		Expect(class_model.fake_constructors.empty(),
			   "defaulted constructor should not generate a fake");
		Expect(class_model.fake_destructors.empty(),
			   "defaulted destructor should not generate a fake");
		Expect(class_model.link_ready, "class should remain link-ready");
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

	[[nodiscard]] bool HasTopLevelUnsupportedKind(const mockfakegen::ClassExtractionResult& result,
												  std::string_view kind)
	{
		for (const auto& item : result.unsupported_items)
		{
			if (item.kind == kind)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] std::size_t UnsupportedKindCount(const mockfakegen::ClassModel& class_model,
												   std::string_view kind)
	{
		std::size_t count = 0U;
		for (const auto& item : class_model.unsupported_items)
		{
			if (item.kind == kind)
			{
				++count;
			}
		}
		return count;
	}

	[[nodiscard]] const mockfakegen::ClassModel&
	FindClass(const mockfakegen::ClassExtractionResult& result, std::string_view name)
	{
		for (const auto& class_model : result.classes)
		{
			if (class_model.name == name)
			{
				return class_model;
			}
		}
		std::cerr << "missing class: " << name << '\n';
		std::exit(1);
	}

	void AvoidsGeneratedNameCollisionsInProductScope()
	{
		TempTree tree;
		tree.Write("include/CollidingNames.h",
				   "#pragma once\n"
				   "namespace app {\n"
				   "class Service { public: void Run(); };\n"
				   "class MockService {};\n"
				   "using ScopedMockService = int;\n"
				   "class FakeService { public: void Run(); };\n"
				   "} // namespace app\n");

		const auto result = ParseAndExtract(tree, "include/CollidingNames.h");

		const auto& service = FindClass(result, "Service");
		Expect(service.qualified_name == "app::Service",
			   "target class should be extracted in namespace");
		Expect(service.mock_name == "MockFakeService",
			   "mock class name should avoid product declaration collision");
		Expect(service.scoped_mock_name == "ScopedMockFakeService",
			   "scoped mock alias should avoid product alias collision");
		const auto& fake_service = FindClass(result, "FakeService");
		Expect(fake_service.mock_name == "MockFakeService2",
			   "mock class name should avoid previously reserved generated name");
		Expect(fake_service.scoped_mock_name == "ScopedMockFakeService2",
			   "scoped mock alias should avoid previously reserved generated alias");
	}

	void RecordsClassTemplateSpecializationsAsUnsupported()
	{
		TempTree tree;
		tree.Write("include/TemplateSpecializations.h",
				   "#pragma once\n"
				   "template <class T> class Box { public: T value; };\n"
				   "template <> class Box<int> { public: int value; };\n"
				   "template <class T> class Box<T*> { public: T* value; };\n"
				   "class Normal { public: bool Run(); };\n");

		const auto result = ParseAndExtract(tree, "include/TemplateSpecializations.h");

		Expect(result.classes.size() == 1U,
			   "template specializations should not become generated classes");
		Expect(result.classes[0].qualified_name == "Normal",
			   "non-template class should still be extracted");
		Expect(HasTopLevelUnsupportedKind(result, "class_template"),
			   "primary class template should be reported unsupported");
		Expect(HasTopLevelUnsupportedKind(result, "class_template_specialization"),
			   "explicit class template specialization should be reported unsupported");
		Expect(HasTopLevelUnsupportedKind(result, "class_template_partial_specialization"),
			   "partial class template specialization should be reported unsupported");
	}

	void ReportsPureVirtualInNormalMode()
	{
		TempTree tree;
		tree.Write("include/AbstractLinkMode.h",
				   "#pragma once\n"
				   "class AbstractLinkMode {\n"
				   "public:\n"
				   "  virtual int Compute() = 0;\n"
				   "  int Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/AbstractLinkMode.h");

		Expect(result.classes.size() == 1U, "abstract normal-mode class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 1U,
			   "pure virtual method should not be generated in normal mode");
		Expect(class_model.mock_methods[0].name == "Supported",
			   "supported concrete declaration should still be generated");
		Expect(HasUnsupportedKind(class_model, "pure_virtual_method"),
			   "pure virtual normal-mode method should be unsupported");
		Expect(!class_model.unsupported_items[0].source_range.begin.file.empty(),
			   "pure virtual unsupported item should keep source location");
	}

	void ReportsOutOfClassInlineDefinition()
	{
		TempTree tree;
		tree.Write("include/OutOfClassInline.h",
				   "#pragma once\n"
				   "class OutOfClassInline {\n"
				   "public:\n"
				   "  void HeaderBody();\n"
				   "  void IncludedBody();\n"
				   "  void Supported();\n"
				   "};\n"
				   "inline void OutOfClassInline::HeaderBody() {}\n"
				   "#include \"OutOfClassInline.inl\"\n");
		tree.Write("include/OutOfClassInline.inl",
				   "#pragma once\n"
				   "inline void OutOfClassInline::IncludedBody() {}\n");

		const auto result = ParseAndExtract(tree, "include/OutOfClassInline.h");

		Expect(result.classes.size() == 1U, "out-of-class inline fixture should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 1U,
			   "header-local out-of-class definition should not be generated");
		Expect(class_model.mock_methods[0].name == "Supported",
			   "declaration-only method should still be generated");
		Expect(UnsupportedKindCount(class_model, "inline_body") == 2U,
			   "header-local and included inline definitions should be reported as inline_body");
	}

	void ReportsMacroOriginMethod()
	{
		TempTree tree;
		tree.Write("include/MacroOrigin.h",
				   "#pragma once\n"
				   "#define MOCKFAKEGEN_DECLARE_METHOD(name) bool name();\n"
				   "class MacroOrigin {\n"
				   "public:\n"
				   "  MOCKFAKEGEN_DECLARE_METHOD(FromMacro)\n"
				   "  bool Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/MacroOrigin.h");

		Expect(result.classes.size() == 1U, "macro-origin fixture should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 1U,
			   "macro-origin method should not be generated");
		Expect(class_model.mock_methods[0].name == "Supported",
			   "non-macro method should still be generated");
		Expect(HasUnsupportedKind(class_model, "macro_origin"),
			   "macro-origin method should be reported unsupported");
	}

	void ReportsConstevalAttributesAndAssignmentOperators()
	{
		TempTree tree;
		tree.Write("include/ModernUnsupported.h",
				   "#pragma once\n"
				   "class ModernUnsupported {\n"
				   "public:\n"
				   "  consteval int Immediate() const { return 1; }\n"
				   "  [[nodiscard]] int Marked();\n"
				   "  [[clang::annotate(\"mockfakegen\")]] int Annotated();\n"
				   "  ModernUnsupported& operator=(const ModernUnsupported&) = default;\n"
				   "  bool Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/ModernUnsupported.h");

		Expect(result.classes.size() == 1U, "modern unsupported fixture should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 2U,
			   "benign attributed method and supported method should be generated");
		Expect(class_model.mock_methods[0].name == "Marked",
			   "nodiscard method should still be generated");
		Expect(class_model.mock_methods[1].name == "Supported",
			   "supported method should still be generated");
		Expect(HasUnsupportedKind(class_model, "consteval_method"),
			   "consteval method should have stable unsupported kind");
		Expect(HasUnsupportedKind(class_model, "unsupported_attribute"),
			   "unknown explicit attribute should remain unsupported");
		Expect(HasUnsupportedKind(class_model, "assignment_operator"),
			   "defaulted assignment operator should have stable unsupported kind");
	}

	void ReportsUnsupportedTypeSpellingCases()
	{
		TempTree tree;
		tree.Write("include/UnsupportedTypes.h",
				   "#pragma once\n"
				   "#define MOCKFAKEGEN_INT_TYPE int\n"
				   "class UnsupportedTypes {\n"
				   "public:\n"
				   "  auto Trailing() -> int;\n"
				   "  decltype(auto) Deduced();\n"
				   "  void (*Factory())(int);\n"
				   "  MOCKFAKEGEN_INT_TYPE MacroType();\n"
				   "  int* _Nonnull NonNull();\n"
				   "  bool Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/UnsupportedTypes.h");

		Expect(result.classes.size() == 1U, "unsupported type fixture should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.mock_methods.size() == 3U,
			   "supported trailing, canonical macro-spelled, and normal methods should generate");
		Expect(class_model.mock_methods[0].name == "Trailing",
			   "concrete trailing return type method should be generated");
		Expect(class_model.mock_methods[0].return_type_spelling == "int",
			   "concrete trailing return type should use AST return spelling");
		Expect(class_model.mock_methods[1].name == "MacroType",
			   "macro-spelled canonical type method should be generated");
		Expect(class_model.mock_methods[1].return_type_spelling == "int",
			   "macro-spelled type should be generated with canonical spelling");
		Expect(class_model.mock_methods[2].name == "Supported",
			   "supported method should still be generated");
		Expect(HasUnsupportedKind(class_model, "decltype_auto_return"),
			   "decltype(auto) return should have stable unsupported kind");
		Expect(HasUnsupportedKind(class_model, "function_pointer_return"),
			   "function pointer return should have stable unsupported kind");
		Expect(HasUnsupportedKind(class_model, "attributed_type"),
			   "attributed type should have stable unsupported kind");
	}

	void ReportsPrivateAliasesAndTemplateArguments()
	{
		TempTree tree;
		tree.Write("include/PrivateTypeUses.h",
				   "#pragma once\n"
				   "#include <vector>\n"
				   "class AliasPrivate {\n"
				   "private:\n"
				   "  using Hidden = int;\n"
				   "public:\n"
				   "  Hidden Get();\n"
				   "  void Put(Hidden value);\n"
				   "  bool Supported();\n"
				   "};\n"
				   "class UsesPrivateTemplateArg {\n"
				   "private:\n"
				   "  class Hidden {};\n"
				   "public:\n"
				   "  std::vector<Hidden> Items();\n"
				   "  void Put(std::vector<Hidden> values);\n"
				   "  int Supported();\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/PrivateTypeUses.h");

		Expect(result.classes.size() == 2U, "private type fixtures should be extracted");
		const auto& alias_private = FindClass(result, "AliasPrivate");
		Expect(alias_private.mock_methods.size() == 1U,
			   "only alias-free method should be generated");
		Expect(alias_private.mock_methods[0].name == "Supported",
			   "alias-free method should remain generated");
		Expect(UnsupportedKindCount(alias_private, "private_nested_type") == 2U,
			   "private alias return and parameter should be unsupported");
		Expect(alias_private.unsupported_items[0].reason.find("AliasPrivate::Hidden") !=
				   std::string::npos,
			   "private alias diagnostic should name inaccessible alias");

		const auto& template_arg = FindClass(result, "UsesPrivateTemplateArg");
		Expect(template_arg.mock_methods.size() == 1U,
			   "only template-argument-free method should be generated");
		Expect(template_arg.mock_methods[0].name == "Supported",
			   "template-argument-free method should remain generated");
		Expect(UnsupportedKindCount(template_arg, "private_nested_type") == 2U,
			   "private nested type in template arguments should be unsupported");
		Expect(template_arg.unsupported_items[0].reason.find("UsesPrivateTemplateArg::Hidden") !=
				   std::string::npos,
			   "template argument diagnostic should name inaccessible nested type");
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

	void SynthesizesUniqueUnnamedParameterNames()
	{
		TempTree tree;
		tree.Write("include/Parameters.h",
				   "#pragma once\n"
				   "class Parameters {\n"
				   "public:\n"
				   "  void Set(int arg1, int, int arg3, int);\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/Parameters.h");

		Expect(result.classes.size() == 1U, "parameter fixture class should be extracted");
		const auto& method = result.classes[0].mock_methods[0];
		Expect(method.parameters.size() == 4U, "all parameters should be extracted");
		Expect(method.parameters[0].generated_name == "arg1",
			   "existing first parameter name should be preserved");
		Expect(method.parameters[1].generated_name == "arg1_2",
			   "unnamed parameter should avoid colliding with arg1");
		Expect(method.parameters[1].declaration_spelling == "int arg1_2",
			   "renamed parameter declaration should use unique name");
		Expect(method.parameters[2].generated_name == "arg3",
			   "existing third parameter name should be preserved");
		Expect(method.parameters[3].generated_name == "arg3_2",
			   "unnamed parameter should avoid colliding with arg3");
		Expect(method.parameters[3].declaration_spelling == "int arg3_2",
			   "second renamed parameter declaration should use unique name");
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
				   "  UnsupportedMethods& operator+=(int delta);\n"
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
		Expect(class_model.mock_methods.size() == 1U,
			   "conditional noexcept should not be generated");
		Expect(class_model.mock_methods[0].name == "Supported",
			   "supported method should be retained");
		Expect(HasUnsupportedKind(class_model, "constructor"), "constructor should be unsupported");
		Expect(HasUnsupportedKind(class_model, "destructor"), "destructor should be unsupported");
		Expect(HasUnsupportedKind(class_model, "conversion_operator"),
			   "conversion operator should be unsupported");
		Expect(HasUnsupportedKind(class_model, "assignment_operator"),
			   "assignment operator should be unsupported with a stable kind");
		Expect(HasUnsupportedKind(class_model, "overloaded_operator"),
			   "non-assignment overloaded operator should be unsupported");
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

	void ExtractsSpecialMembersWhenEnabled()
	{
		TempTree tree;
		tree.Write("include/Special.h",
				   "#pragma once\n"
				   "class Base {\n"
				   "public:\n"
				   "  Base() = default;\n"
				   "};\n"
				   "class Special : public Base {\n"
				   "public:\n"
				   "  explicit Special(int value) noexcept;\n"
				   "  ~Special() noexcept;\n"
				   "  bool Touch();\n"
				   "private:\n"
				   "  int value_;\n"
				   "  const int limit_;\n"
				   "  int initialized_ = 7;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/Special.h",
							mockfakegen::ClassExtractionOptions{.fake_special_members = true});

		Expect(result.classes.size() == 2U, "base and special classes should be extracted");
		const auto& class_model =
			result.classes[0].name == "Special" ? result.classes[0] : result.classes[1];
		Expect(class_model.fake_constructors.size() == 1U,
			   "safe constructor should be generated as fake constructor");
		Expect(class_model.fake_constructors[0].is_noexcept,
			   "constructor noexcept should be preserved");
		Expect(class_model.fake_constructors[0].member_initializers.size() == 2U,
			   "constructor should initialize default-constructible field");
		Expect(class_model.fake_constructors[0].member_initializers[0] == "value_{}",
			   "constructor initializer should be deterministic");
		Expect(class_model.fake_constructors[0].member_initializers[1] == "limit_{}",
			   "const field initializer should be synthesized");
		Expect(class_model.fake_destructors.size() == 1U,
			   "out-of-line destructor should be generated as fake destructor");
		Expect(class_model.fake_destructors[0].is_noexcept,
			   "destructor noexcept should be preserved");
		Expect(class_model.unsupported_items.empty(),
			   "safe special members should not be reported unsupported");
	}

	void ReportsUnsafeSpecialMemberConstructor()
	{
		TempTree tree;
		tree.Write("include/UnsafeSpecial.h",
				   "#pragma once\n"
				   "class UnsafeSpecial {\n"
				   "public:\n"
				   "  UnsafeSpecial();\n"
				   "  bool Touch();\n"
				   "private:\n"
				   "  int& ref_;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/UnsafeSpecial.h",
							mockfakegen::ClassExtractionOptions{.fake_special_members = true});

		Expect(result.classes.size() == 1U, "unsafe special fixture class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.fake_constructors.empty(), "unsafe constructor should not be generated");
		Expect(HasUnsupportedKind(class_model, "constructor"),
			   "unsafe constructor should be reported unsupported");
		Expect(!class_model.unsupported_items.empty(), "unsafe constructor should keep reason");
		Expect(class_model.unsupported_items[0].reason.find("reference member") !=
				   std::string::npos,
			   "unsafe constructor diagnostic should explain reference member");
	}

	void ReportsUnsafeSpecialMemberBaseConstructor()
	{
		TempTree tree;
		tree.Write("include/UnsafeBase.h",
				   "#pragma once\n"
				   "class Base {\n"
				   "public:\n"
				   "  explicit Base(int value);\n"
				   "};\n"
				   "class Derived : public Base {\n"
				   "public:\n"
				   "  Derived();\n"
				   "  bool Touch();\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/UnsafeBase.h",
							mockfakegen::ClassExtractionOptions{.fake_special_members = true});

		Expect(result.classes.size() == 2U, "base and derived classes should be extracted");
		const auto& derived_model = result.classes[1];
		Expect(derived_model.name == "Derived", "derived class should be checked");
		Expect(derived_model.fake_constructors.empty(),
			   "unsafe derived constructor should not be generated");
		Expect(HasUnsupportedKind(derived_model, "constructor"),
			   "unsafe base initialization should be reported unsupported");
		Expect(!derived_model.unsupported_items.empty(), "unsafe base should keep reason");
		Expect(derived_model.unsupported_items[0].reason.find("base class") != std::string::npos,
			   "unsafe constructor diagnostic should explain base class");
	}

	void ReportsUnsafeSpecialMemberPolicyBranches()
	{
		TempTree tree;
		tree.Write("include/UnsafeSpecialBranches.h",
				   "#pragma once\n"
				   "class NoDefault {\n"
				   "public:\n"
				   "  explicit NoDefault(int value);\n"
				   "};\n"
				   "class UnsafeField {\n"
				   "public:\n"
				   "  UnsafeField();\n"
				   "private:\n"
				   "  NoDefault field_;\n"
				   "};\n"
				   "class PrivateParameter {\n"
				   "  class Token {};\n"
				   "public:\n"
				   "  explicit PrivateParameter(Token token);\n"
				   "};\n"
				   "class ThrowingSpecial {\n"
				   "public:\n"
				   "  ThrowingSpecial() noexcept(false);\n"
				   "  ~ThrowingSpecial() noexcept(false);\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/UnsafeSpecialBranches.h",
							mockfakegen::ClassExtractionOptions{.fake_special_members = true});

		Expect(result.classes.size() == 4U, "all unsafe policy classes should be extracted");
		const auto& unsafe_field = FindClass(result, "UnsafeField");
		Expect(unsafe_field.fake_constructors.empty(),
			   "constructor with non-default field should not be generated");
		Expect(HasUnsupportedKind(unsafe_field, "constructor"),
			   "non-default field should report constructor unsupported");
		Expect(unsafe_field.unsupported_items[0].reason.find("not default-constructible") !=
				   std::string::npos,
			   "non-default field reason should be specific");

		const auto& private_parameter = FindClass(result, "PrivateParameter");
		Expect(private_parameter.fake_constructors.empty(),
			   "constructor with private parameter type should not be generated");
		Expect(HasUnsupportedKind(private_parameter, "private_nested_type"),
			   "private parameter type should report stable unsupported kind");

		const auto& throwing_special = FindClass(result, "ThrowingSpecial");
		Expect(throwing_special.fake_constructors.empty(),
			   "throwing constructor should not be generated");
		Expect(throwing_special.fake_destructors.empty(),
			   "throwing destructor should not be generated");
		Expect(UnsupportedKindCount(throwing_special, "constructor") == 1U,
			   "throwing constructor should report constructor unsupported");
		Expect(UnsupportedKindCount(throwing_special, "destructor") == 1U,
			   "throwing destructor should report destructor unsupported");
	}

	void ExtractsStaticDataWhenEnabled()
	{
		TempTree tree;
		tree.Write("include/StaticData.h",
				   "#pragma once\n"
				   "class StaticData {\n"
				   "public:\n"
				   "  static int count;\n"
				   "  static const int limit;\n"
				   "  inline static int inline_count = 3;\n"
				   "  static constexpr int cached = 9;\n"
				   "  static bool Ready();\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/StaticData.h",
							mockfakegen::ClassExtractionOptions{.fake_static_data = true});

		Expect(result.classes.size() == 1U, "static data fixture class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.static_data_members.size() == 2U,
			   "out-of-line static data should be extracted");
		Expect(class_model.static_data_members[0].name == "count",
			   "first static data member should keep declaration order");
		Expect(class_model.static_data_members[0].type_spelling == "int",
			   "static data type should be spelled");
		Expect(class_model.static_data_members[1].name == "limit",
			   "const static data member should be extracted");
		Expect(class_model.static_data_members[1].type_spelling == "const int",
			   "const static data type should preserve const");
		Expect(class_model.mock_methods.size() == 1U,
			   "static member functions should still be extracted separately");
		Expect(class_model.unsupported_items.empty(),
			   "safe static data members should not be reported unsupported");
	}

	void ReportsStaticDataWhenDisabled()
	{
		TempTree tree;
		tree.Write("include/StaticDataDisabled.h",
				   "#pragma once\n"
				   "class StaticDataDisabled {\n"
				   "public:\n"
				   "  static int count;\n"
				   "};\n");

		const auto result = ParseAndExtract(tree, "include/StaticDataDisabled.h");

		Expect(result.classes.size() == 1U, "disabled static data class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.static_data_members.empty(),
			   "static data should not be generated when disabled");
		Expect(HasUnsupportedKind(class_model, "static_data_member"),
			   "disabled static data should be reported unsupported");
		Expect(!result.diagnostics.empty(), "disabled static data should emit diagnostic");
	}

	void ReportsUnsafeStaticDataWhenEnabled()
	{
		TempTree tree;
		tree.Write("include/UnsafeStaticData.h",
				   "#pragma once\n"
				   "class NoDefaultStatic {\n"
				   "public:\n"
				   "  explicit NoDefaultStatic(int value);\n"
				   "};\n"
				   "class UnsafeStaticData {\n"
				   "  class PrivateToken {};\n"
				   "public:\n"
				   "  struct PublicToken {};\n"
				   "  static int& ref;\n"
				   "  static constinit int boot;\n"
				   "  static int values[2];\n"
				   "  static thread_local int tls;\n"
				   "  static const int initialized = 7;\n"
				   "  static PublicToken public_token;\n"
				   "  static PrivateToken private_token;\n"
				   "  static NoDefaultStatic object;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/UnsafeStaticData.h",
							mockfakegen::ClassExtractionOptions{.fake_static_data = true});

		Expect(result.classes.size() == 2U, "unsafe static data classes should be extracted");
		const auto& class_model = result.classes[1];
		Expect(class_model.static_data_members.empty(),
			   "unsafe static data should not be generated");
		Expect(UnsupportedKindCount(class_model, "static_data_member") == 8U,
			   "all unsafe static data members should be reported unsupported");
		Expect(class_model.unsupported_items[0].reason.find("reference static data member") !=
				   std::string::npos,
			   "reference static data diagnostic should be specific");
		Expect(class_model.unsupported_items[1].reason.find("constinit") != std::string::npos,
			   "constinit static data diagnostic should be specific");
		Expect(class_model.unsupported_items[2].reason.find("array static data member") !=
				   std::string::npos,
			   "array static data diagnostic should be specific");
		Expect(class_model.unsupported_items[3].reason.find("thread-local") != std::string::npos,
			   "thread-local static data diagnostic should be specific");
		Expect(class_model.unsupported_items[4].reason.find("in-class initializer") !=
				   std::string::npos,
			   "initializer-dependent static data diagnostic should be specific");
		Expect(class_model.unsupported_items[5].reason.find("nested type") != std::string::npos,
			   "public nested static data diagnostic should be specific");
		Expect(class_model.unsupported_items[6].reason.find("nested type") != std::string::npos,
			   "private nested static data diagnostic should be specific");
		Expect(class_model.unsupported_items[7].reason.find("not default-constructible") !=
				   std::string::npos,
			   "non-default static data diagnostic should be specific");
	}

	void ExtractsInterfaceMockWhenEnabled()
	{
		TempTree tree;
		tree.Write("include/IStorage.h",
				   "#pragma once\n"
				   "class IStorage {\n"
				   "public:\n"
				   "  virtual ~IStorage() = default;\n"
				   "  virtual bool Save(const char* key, int value) const noexcept = 0;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/IStorage.h",
							mockfakegen::ClassExtractionOptions{.interface_mock = true});

		Expect(result.classes.size() == 1U, "interface class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.interface_mock, "interface model should be marked");
		Expect(class_model.mock_destructor_override,
			   "explicit virtual interface destructor should request mock destructor override");
		Expect(class_model.mock_methods.size() == 1U,
			   "pure virtual method should be extracted for interface mock");
		Expect(class_model.fake_methods.empty(), "interface mode should not create fake methods");
		Expect(class_model.mock_methods[0].name == "Save",
			   "interface method name should be extracted");
		Expect(class_model.mock_methods[0].is_virtual &&
				   class_model.mock_methods[0].is_pure_virtual,
			   "interface method should keep virtual flags");
		Expect(class_model.mock_methods[0].is_const, "interface const qualifier should be kept");
		Expect(class_model.mock_methods[0].is_noexcept,
			   "interface noexcept qualifier should be kept");
		Expect(class_model.unsupported_items.empty(),
			   "pure interface should not be reported unsupported");
	}

	void InterfaceModeSupportsImplicitNonVirtualDestructor()
	{
		TempTree tree;
		tree.Write("include/ImplicitDtorIface.h",
				   "#pragma once\n"
				   "class ImplicitDtorIface {\n"
				   "public:\n"
				   "  virtual int Run() = 0;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/ImplicitDtorIface.h",
							mockfakegen::ClassExtractionOptions{.interface_mock = true});

		Expect(result.classes.size() == 1U,
			   "implicit-destructor interface class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.interface_mock, "implicit-destructor interface should be marked");
		Expect(!class_model.mock_destructor_override,
			   "implicit non-virtual destructor should not request override");
		Expect(class_model.mock_methods.size() == 1U,
			   "pure virtual method should still be generated");
		Expect(class_model.mock_methods[0].name == "Run",
			   "interface method name should be extracted");
		Expect(class_model.unsupported_items.empty(),
			   "implicit non-virtual destructor should not make the interface unsupported");
	}

	void ExtractsConcreteVirtualInterfaceMock()
	{
		TempTree tree;
		tree.Write("include/ConcreteVirtual.h",
				   "#pragma once\n"
				   "class ConcreteVirtual {\n"
				   "public:\n"
				   "  virtual ~ConcreteVirtual() = default;\n"
				   "  virtual int Run() { return 1; }\n"
				   "  virtual int Count() const;\n"
				   "  bool Helper();\n"
				   "  static int StaticCount();\n"
				   "  static int Data;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/ConcreteVirtual.h",
							mockfakegen::ClassExtractionOptions{.interface_mock = true});

		Expect(result.classes.size() == 1U, "concrete virtual class should be extracted");
		const auto& class_model = result.classes[0];
		Expect(class_model.interface_mock, "concrete virtual model should be marked");
		Expect(class_model.mock_methods.size() == 2U,
			   "public virtual methods should be extracted for interface mock");
		Expect(class_model.fake_methods.empty(),
			   "concrete virtual interface mode should not create fake methods");
		Expect(class_model.mock_methods[0].name == "Run",
			   "inline concrete virtual method should be extracted");
		Expect(!class_model.mock_methods[0].is_pure_virtual,
			   "concrete virtual method should keep non-pure flag");
		Expect(class_model.mock_methods[1].name == "Count",
			   "declared concrete virtual method should be extracted");
		Expect(class_model.mock_methods[1].is_const,
			   "concrete virtual const qualifier should be kept");
		Expect(class_model.unsupported_items.empty(),
			   "safe non-virtual members should be ignored in interface mode");
	}

	void ReportsFinalInterfaceConstructs()
	{
		TempTree tree;
		tree.Write("include/FinalInterface.h",
				   "#pragma once\n"
				   "class FinalIface final {\n"
				   "public:\n"
				   "  virtual ~FinalIface() = default;\n"
				   "  virtual void Run() = 0;\n"
				   "};\n"
				   "class MethodFinalIface {\n"
				   "public:\n"
				   "  virtual ~MethodFinalIface() = default;\n"
				   "  virtual void Run() final = 0;\n"
				   "  virtual int Keep() = 0;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/FinalInterface.h",
							mockfakegen::ClassExtractionOptions{.interface_mock = true});

		Expect(result.classes.size() == 2U, "final interface fixture should extract two classes");
		const auto& final_class = FindClass(result, "FinalIface");
		Expect(!final_class.interface_mock,
			   "final interface class should not remain in deriving mock mode");
		Expect(final_class.mock_methods.empty(), "final interface class should not emit overrides");
		Expect(UnsupportedKindCount(final_class, "interface_construct") == 1U,
			   "final interface class should be unsupported");
		Expect(final_class.unsupported_items[0].reason.find("final interface class") !=
				   std::string::npos,
			   "final class diagnostic should be specific");

		const auto& final_method = FindClass(result, "MethodFinalIface");
		Expect(final_method.interface_mock, "method-final interface should remain interface mode");
		Expect(final_method.mock_methods.size() == 1U,
			   "non-final pure virtual method should still be generated");
		Expect(final_method.mock_methods[0].name == "Keep",
			   "final pure virtual method should be skipped");
		Expect(UnsupportedKindCount(final_method, "interface_construct") == 1U,
			   "final method should be unsupported");
		Expect(final_method.unsupported_items[0].reason.find("final virtual method") !=
				   std::string::npos,
			   "final method diagnostic should be specific");
	}

	void InterfaceModeKeepsConcreteClassesLinkSeam()
	{
		TempTree tree;
		tree.Write("include/MixedInterface.h",
				   "#pragma once\n"
				   "class Concrete {\n"
				   "public:\n"
				   "  bool Run();\n"
				   "};\n"
				   "class IStorage {\n"
				   "public:\n"
				   "  virtual ~IStorage() = default;\n"
				   "  virtual bool Save() = 0;\n"
				   "};\n");

		const auto result =
			ParseAndExtract(tree,
							"include/MixedInterface.h",
							mockfakegen::ClassExtractionOptions{.interface_mock = true});

		Expect(result.classes.size() == 2U, "mixed interface fixture should extract two classes");
		const auto& concrete =
			result.classes[0].qualified_name == "Concrete" ? result.classes[0] : result.classes[1];
		const auto& interface_model =
			result.classes[0].qualified_name == "IStorage" ? result.classes[0] : result.classes[1];
		Expect(!concrete.interface_mock,
			   "non-virtual concrete class should remain link-seam in interface option mode");
		Expect(concrete.mock_methods.size() == 1U, "concrete mock method should be extracted");
		Expect(concrete.fake_methods.size() == 1U, "concrete fake method should be extracted");
		Expect(interface_model.interface_mock, "pure interface should be marked interface mock");
		Expect(interface_model.mock_methods.size() == 1U,
			   "pure interface mock method should be extracted");
		Expect(interface_model.fake_methods.empty(),
			   "pure interface should not create fake methods");
	}
} // namespace

int main()
{
	ExtractsGlobalClass();
	ExtractsNamespacedClass();
	AvoidsGeneratedNameCollisionsInProductScope();
	RecordsTopLevelStructAsUnsupported();
	IgnoresDefaultedSpecialMembersWithoutFakeSpecialMembers();
	RecordsClassTemplateAsUnsupported();
	RecordsClassTemplateSpecializationsAsUnsupported();
	ReportsPureVirtualInNormalMode();
	ReportsOutOfClassInlineDefinition();
	ReportsMacroOriginMethod();
	ReportsConstevalAttributesAndAssignmentOperators();
	ReportsUnsupportedTypeSpellingCases();
	ReportsPrivateAliasesAndTemplateArguments();
	ExtractsPublicMethodsAndQualifiersInDeclarationOrder();
	SynthesizesUniqueUnnamedParameterNames();
	RecordsUnsupportedMethodConstructs();
	ExtractsSpecialMembersWhenEnabled();
	ReportsUnsafeSpecialMemberConstructor();
	ReportsUnsafeSpecialMemberBaseConstructor();
	ReportsUnsafeSpecialMemberPolicyBranches();
	ExtractsStaticDataWhenEnabled();
	ReportsStaticDataWhenDisabled();
	ReportsUnsafeStaticDataWhenEnabled();
	ExtractsInterfaceMockWhenEnabled();
	InterfaceModeSupportsImplicitNonVirtualDestructor();
	ExtractsConcreteVirtualInterfaceMock();
	ReportsFinalInterfaceConstructs();
	InterfaceModeKeepsConcreteClassesLinkSeam();
	return 0;
}
