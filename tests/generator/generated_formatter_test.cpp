#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	class TempTree
	{
	  public:
		TempTree()
			: root_(std::filesystem::temp_directory_path() /
					("mockfakegen_generated_formatter_test_" + std::to_string(UniqueSuffix())))
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
			Expect(stream.good(), "temp file should be written");
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	[[nodiscard]] mockfakegen::GeneratedFile CxxFile(std::string_view content)
	{
		return mockfakegen::MakeGeneratedFile(
			"MockHoge.h", std::string(content), mockfakegen::GeneratedFileKind::MockHeader);
	}

	[[nodiscard]] mockfakegen::GeneratedFormatOptions
	FormatOptions(mockfakegen::FormatStyleKind style)
	{
		mockfakegen::GeneratedFormatOptions options;
		options.style = style;
		return options;
	}

	void FileStyleUsesClangFormatFile()
	{
		TempTree tree;
		tree.Write(".clang-format",
				   "BasedOnStyle: LLVM\n"
				   "BreakBeforeBraces: Allman\n"
				   "IndentWidth: 2\n"
				   "UseTab: Never\n");
		const std::vector files = {CxxFile("void f(){if(true){return;}}\n")};

		const auto result = mockfakegen::FormatGeneratedFiles(
			mockfakegen::GeneratedFormatOptions{
				.style = mockfakegen::FormatStyleKind::File,
				.style_search_root = tree.root(),
			},
			files);

		Expect(result.ok(), "file style with .clang-format should format");
		Expect(Contains(result.files[0].content, "void f()\n{"),
			   "file style should use Allman braces from .clang-format");
		Expect(Contains(result.files[0].content, "  if (true)\n  {"),
			   "file style should use configured indentation");
	}

	void FileStyleFallsBackToLlvmWithoutClangFormatFile()
	{
		TempTree tree;
		const std::vector files = {CxxFile("class Hoge{public:void Run();};\n")};

		const auto file_result = mockfakegen::FormatGeneratedFiles(
			mockfakegen::GeneratedFormatOptions{
				.style = mockfakegen::FormatStyleKind::File,
				.style_search_root = tree.root(),
			},
			files);
		const auto llvm_result = mockfakegen::FormatGeneratedFiles(
			mockfakegen::GeneratedFormatOptions{
				.style = mockfakegen::FormatStyleKind::Llvm,
				.style_search_root = tree.root(),
			},
			files);

		Expect(file_result.ok(), "file style without .clang-format should use fallback");
		Expect(llvm_result.ok(), "llvm style should format");
		Expect(file_result.files[0].content == llvm_result.files[0].content,
			   "file style fallback should be deterministic LLVM");
	}

	void SupportsLlvmAndGoogleStyles()
	{
		const std::vector files = {CxxFile("class Hoge{public:void Run();};\n")};

		const auto llvm_result = mockfakegen::FormatGeneratedFiles(
			FormatOptions(mockfakegen::FormatStyleKind::Llvm), files);
		const auto google_result = mockfakegen::FormatGeneratedFiles(
			FormatOptions(mockfakegen::FormatStyleKind::Google), files);

		Expect(llvm_result.ok(), "llvm style should format");
		Expect(google_result.ok(), "google style should format");
		Expect(Contains(llvm_result.files[0].content, "public:\n  void Run();"),
			   "LLVM style should leave public access unindented");
		Expect(Contains(google_result.files[0].content, " public:\n  void Run();"),
			   "Google style should indent public access");
	}

	void SortsIncludesDeterministically()
	{
		const std::vector files = {CxxFile("#include \"Zeta.h\"\n#include \"Alpha.h\"\n")};

		const auto result = mockfakegen::FormatGeneratedFiles(
			FormatOptions(mockfakegen::FormatStyleKind::Llvm), files);

		Expect(result.ok(), "include sorting format should succeed");
		Expect(result.files[0].content == "#include \"Alpha.h\"\n#include \"Zeta.h\"\n",
			   "formatter should sort include blocks deterministically");
	}

	void NoneStylePreservesContentButNormalizesLf()
	{
		const std::vector files = {CxxFile("class Hoge{public:void Run();};\r\n")};

		const auto result = mockfakegen::FormatGeneratedFiles(
			FormatOptions(mockfakegen::FormatStyleKind::None), files);

		Expect(result.ok(), "none style should not report diagnostics");
		Expect(result.files[0].content == "class Hoge{public:void Run();};\n",
			   "none style should preserve layout but keep LF trailing newline");
	}

	void ReportsFormatFailure()
	{
		TempTree tree;
		tree.Write(".clang-format", "BasedOnStyle: LLVM\nIndentWidth: nope\n");
		const std::vector files = {CxxFile("void f(){return;}\n")};

		const auto result = mockfakegen::FormatGeneratedFiles(
			mockfakegen::GeneratedFormatOptions{
				.style = mockfakegen::FormatStyleKind::File,
				.style_search_root = tree.root(),
			},
			files);

		Expect(!result.ok(), "format failure should be reported");
		Expect(result.diagnostics.size() == 1U, "format failure should produce one diagnostic");
		Expect(result.diagnostics[0].path == "MockHoge.h",
			   "format diagnostic should identify generated file");
		Expect(Contains(result.diagnostics[0].message, "failed to resolve clang-format style"),
			   "format diagnostic should explain style resolution failure");
	}

	void RepeatedFormattingIsDeterministic()
	{
		const std::vector files = {CxxFile("class Hoge{public:void Run();};\n")};
		const auto options = FormatOptions(mockfakegen::FormatStyleKind::Google);

		const auto first = mockfakegen::FormatGeneratedFiles(options, files);
		const auto second = mockfakegen::FormatGeneratedFiles(options, files);

		Expect(first.ok(), "first format should succeed");
		Expect(second.ok(), "second format should succeed");
		Expect(first.files[0].content == second.files[0].content,
			   "same input should format deterministically");
	}
} // namespace

int main()
{
	FileStyleUsesClangFormatFile();
	FileStyleFallsBackToLlvmWithoutClangFormatFile();
	SupportsLlvmAndGoogleStyles();
	SortsIncludesDeterministically();
	NoneStylePreservesContentButNormalizesLf();
	ReportsFormatFailure();
	RepeatedFormattingIsDeterministic();
	return 0;
}
