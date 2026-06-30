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
} // namespace

int main()
{
	ExtractsGlobalClass();
	ExtractsNamespacedClass();
	SkipsForwardDeclarationAnonymousStructAndSystemHeaders();
	RecordsClassTemplateAsUnsupported();
	return 0;
}
