#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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
	EmptyDirectoryIsSuccessful();
	MissingRootProducesDiagnostic();
	FileRootProducesDiagnostic();
	return 0;
}
