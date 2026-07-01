#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "HeaderScanner.h"

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
					("mockfakegen_header_scanner_test_" + std::to_string(UniqueSuffix())))
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

		void Write(std::string_view relative_path,
				   std::string_view content = "#pragma once\n") const
		{
			const auto path = root_ / std::filesystem::path(relative_path);
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path);
			stream << content;
			Expect(stream.good(), "test fixture file should be written");
		}

		void MakeDirectory(std::string_view relative_path) const
		{
			std::filesystem::create_directories(root_ / std::filesystem::path(relative_path));
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	[[nodiscard]] std::vector<std::string>
	IncludeSpellings(const std::vector<mockfakegen::HeaderCandidate>& headers)
	{
		std::vector<std::string> values;
		values.reserve(headers.size());
		for (const auto& header : headers)
		{
			values.push_back(header.include_spelling);
		}
		return values;
	}

	[[nodiscard]] bool HasDiagnosticCode(const mockfakegen::HeaderScanResult& result,
										 mockfakegen::HeaderScanDiagnosticCode code)
	{
		for (const auto& diagnostic : result.diagnostics)
		{
			if (diagnostic.code == code)
			{
				return true;
			}
		}
		return false;
	}

	void FindsNestedHeadersInDeterministicOrder()
	{
		TempTree tree;
		tree.Write("include/zeta/Z.h");
		tree.Write("include/alpha/A.h");
		tree.Write("include/alpha/B.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "nested header scan should not produce diagnostics");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{
			"include/alpha/A.h",
			"include/alpha/B.h",
			"include/zeta/Z.h",
		};
		Expect(includes == expected, "headers should be sorted by project-relative path");
		Expect(result.headers.front().absolute_path.is_absolute(),
			   "absolute path should be stored");
		Expect(result.headers.front().project_relative_path == "include/alpha/A.h",
			   "project-relative path should be stored");
	}

	void IgnoresNonScopeHeaderExtensions()
	{
		TempTree tree;
		tree.Write("include/A.h");
		tree.Write("include/B.hpp");
		tree.Write("include/C.hh");
		tree.Write("include/D.txt");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "extension filtering should not produce diagnostics");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/A.h"};
		Expect(includes == expected, "only .h files should be included in the initial policy");
	}

	void ExcludesOutputDirectory()
	{
		TempTree tree;
		tree.Write("include/Product.h");
		tree.Write("include/generated/MockProduct.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "include/generated",
		});

		Expect(result.ok(), "output directory exclusion should not produce diagnostics");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/Product.h"};
		Expect(includes == expected, "output directory should be excluded from scanning");
		Expect(HasDiagnosticCode(result,
								 mockfakegen::HeaderScanDiagnosticCode::SkippedGeneratedOutput),
			   "output directory skip should be diagnosed");
	}

	void ExcludesPriorGeneratedOutputAndGeneratedHeaders()
	{
		TempTree tree;
		tree.Write("include/Product.h");
		tree.Write("include/prior/MockFakeRuntime.h");
		tree.Write("include/prior/AllMocks.h");
		tree.Write("include/prior/MockProduct.h",
				   "#pragma once\n"
				   "#include <gmock/gmock.h>\n"
				   "#include \"MockFakeRuntime.h\"\n"
				   "class MockProduct {};\n");
		tree.Write("include/stray/MockStray.h",
				   "#pragma once\n"
				   "#include <gmock/gmock.h>\n"
				   "#include \"MockFakeRuntime.h\"\n"
				   "class MockStray {};\n");
		tree.Write("include/marked/Generated.h",
				   "#pragma once\n"
				   "// Generated by mockfakegen\n"
				   "class Generated {};\n");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "current-output",
		});

		Expect(result.ok(), "generated output exclusion should not be fatal");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/Product.h"};
		Expect(includes == expected, "generated headers should not be rediscovered");
		Expect(HasDiagnosticCode(result,
								 mockfakegen::HeaderScanDiagnosticCode::SkippedGeneratedOutput),
			   "generated output skip should be diagnosed");
	}

	void ExcludesBuiltInDirectories()
	{
		TempTree tree;
		tree.Write("include/Product.h");
		tree.Write("include/third_party/Vendor.h");
		tree.Write("include/external/External.h");
		tree.Write("include/build/BuildArtifact.h");
		tree.Write("include/cmake-build-debug/CMakeArtifact.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "built-in exclusion should not be fatal");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/Product.h"};
		Expect(includes == expected, "built-in excluded directories should be skipped");
		Expect(
			HasDiagnosticCode(result, mockfakegen::HeaderScanDiagnosticCode::SkippedExcludedPath),
			"built-in excluded path skip should be diagnosed");
	}

	void AllowsExplicitInputRootUnderBuiltInDirectory()
	{
		TempTree tree;
		tree.Write("third_party/vendor/include/Foo.h");
		tree.Write("third_party/vendor/include/build/BuildArtifact.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "third_party/vendor/include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "explicit built-in input root should scan successfully");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"third_party/vendor/include/Foo.h"};
		Expect(includes == expected,
			   "built-in exclusion should not apply to explicit input-root prefix");
		Expect(
			HasDiagnosticCode(result, mockfakegen::HeaderScanDiagnosticCode::SkippedExcludedPath),
			"built-in excluded child path should still be diagnosed");
	}

	void AppliesConfiguredExcludesAndHeaderFilter()
	{
		TempTree tree;
		tree.Write("include/public/Keep.h");
		tree.Write("include/public/Drop.hpp");
		tree.Write("include/internal/Hidden.h");
		tree.Write("include/experimental/Maybe.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
			.header_filter = "public/Keep\\.h$",
			.exclude_globs = {"include/internal/**"},
		});

		Expect(result.ok(), "configured filters should not be fatal");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/public/Keep.h"};
		Expect(includes == expected, "configured exclude and header filter should be applied");
		Expect(
			HasDiagnosticCode(result, mockfakegen::HeaderScanDiagnosticCode::SkippedExcludedPath),
			"configured filtered paths should be diagnosed");
	}

	void SkipsSymlinkPaths()
	{
		TempTree tree;
		tree.Write("include/Product.h");
		tree.Write("outside/Linked.h");
		std::error_code symlink_error;
		std::filesystem::create_symlink(
			tree.root() / "outside/Linked.h", tree.root() / "include/Linked.h", symlink_error);
		if (symlink_error)
		{
			return;
		}
		std::filesystem::create_directory_symlink(
			tree.root() / "include", tree.root() / "include/loop", symlink_error);
		if (symlink_error)
		{
			return;
		}

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "symlink skip should not be fatal");
		const auto includes = IncludeSpellings(result.headers);
		const std::vector<std::string> expected{"include/Product.h"};
		Expect(includes == expected, "symlink files and directories should be skipped");
		Expect(HasDiagnosticCode(result, mockfakegen::HeaderScanDiagnosticCode::SkippedSymlinkPath),
			   "symlink skip should be diagnosed");
	}

	void EmptyDirectoryIsSuccessful()
	{
		TempTree tree;
		tree.MakeDirectory("include");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(result.ok(), "empty directory should scan successfully");
		Expect(result.headers.empty(), "empty directory should have no headers");
	}

	void MissingRootProducesDiagnostic()
	{
		TempTree tree;
		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "missing",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(!result.ok(), "missing input root should produce diagnostic");
		Expect(result.headers.empty(), "missing input root should produce no headers");
		Expect(result.diagnostics.size() == 1U, "missing input root should produce one diagnostic");
		Expect(result.diagnostics[0].code ==
				   mockfakegen::HeaderScanDiagnosticCode::InputRootDoesNotExist,
			   "missing input root diagnostic code should be stable");
	}

	void FileRootProducesDiagnostic()
	{
		TempTree tree;
		tree.Write("include/Product.h");

		const auto result = mockfakegen::ScanHeaders({
			.input_root = tree.root() / "include/Product.h",
			.project_root = tree.root(),
			.output_dir = tree.root() / "generated",
		});

		Expect(!result.ok(), "file input root should produce diagnostic");
		Expect(result.headers.empty(), "file input root should produce no headers");
		Expect(result.diagnostics.size() == 1U, "file input root should produce one diagnostic");
		Expect(result.diagnostics[0].code ==
				   mockfakegen::HeaderScanDiagnosticCode::InputRootIsNotDirectory,
			   "file input root diagnostic code should be stable");
	}
} // namespace

int main()
{
	FindsNestedHeadersInDeterministicOrder();
	IgnoresNonScopeHeaderExtensions();
	ExcludesOutputDirectory();
	ExcludesPriorGeneratedOutputAndGeneratedHeaders();
	ExcludesBuiltInDirectories();
	AllowsExplicitInputRootUnderBuiltInDirectory();
	AppliesConfiguredExcludesAndHeaderFilter();
	SkipsSymlinkPaths();
	EmptyDirectoryIsSuccessful();
	MissingRootProducesDiagnostic();
	FileRootProducesDiagnostic();
	return 0;
}
