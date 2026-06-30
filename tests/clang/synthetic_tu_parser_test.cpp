#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

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
					("mockfakegen_synthetic_tu_parser_test_" + std::to_string(UniqueSuffix())))
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

	[[nodiscard]] bool HasCompileArg(const std::vector<std::string>& args,
									 std::string_view expected)
	{
		for (const auto& arg : args)
		{
			if (arg == expected)
			{
				return true;
			}
		}
		return false;
	}

	void BuildsSyntheticTuCode()
	{
		const auto code = mockfakegen::BuildSyntheticTuCode("include/Hoge.h");
		Expect(code == "#include \"include/Hoge.h\"\n",
			   "synthetic TU code should be deterministic");
	}

	void BuildsFallbackCompileArgs()
	{
		TempTree tree;
		const auto args = mockfakegen::BuildSyntheticTuFallbackArgs(tree.root());

		Expect(HasCompileArg(args, "-std=c++23"), "fallback args should include C++23");
		Expect(HasCompileArg(
				   args, "-I" + std::filesystem::weakly_canonical(tree.root()).generic_string()),
			   "fallback args should include project root include path");
	}

	void ParsesSimpleHeader()
	{
		TempTree tree;
		tree.Write("include/Hoge.h", "#pragma once\nclass Hoge { public: bool DoSomething(); };\n");

		const auto result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / "include/Hoge.h",
			.project_root = tree.root(),
		});

		Expect(result.success, "simple header should parse through synthetic TU");
		Expect(result.ast != nullptr, "successful parse should keep AST");
		Expect(result.diagnostics.empty(), "successful parse should not report diagnostics");
		Expect(result.header.parsed_by_synthetic_tu, "header should record synthetic parse mode");
		Expect(result.header.include_spelling == "include/Hoge.h",
			   "header should keep project-relative include spelling");
		Expect(result.synthetic_code == "#include \"include/Hoge.h\"\n",
			   "result should expose synthetic code");
		Expect(HasCompileArg(result.compile_args, "-std=c++23"),
			   "result should expose C++23 compile arg");
		Expect(
			HasCompileArg(result.compile_args,
						  "-I" + std::filesystem::weakly_canonical(tree.root()).generic_string()),
			"result should expose project include arg");
	}

	void ReportsParseErrors()
	{
		TempTree tree;
		tree.Write("include/Broken.h", "#pragma once\nclass Broken { public: void nope( ; };\n");

		const auto result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = tree.root() / "include/Broken.h",
			.project_root = tree.root(),
		});

		Expect(!result.success, "broken header should not parse successfully");
		Expect(!result.diagnostics.empty(), "broken header should return diagnostics");
		Expect(result.diagnostics[0].severity == mockfakegen::ClangDiagnosticSeverity::Error,
			   "parse failure should report error severity");
		Expect(!result.diagnostics[0].message.empty(), "parse diagnostic should include message");
		Expect(result.header.parsed_by_synthetic_tu,
			   "failed parse should still record synthetic parse mode");
	}
} // namespace

int main()
{
	BuildsSyntheticTuCode();
	BuildsFallbackCompileArgs();
	ParsesSimpleHeader();
	ReportsParseErrors();
	return 0;
}
