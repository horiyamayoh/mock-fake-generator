#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "output/OutputWriter.h"

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
					("mockfakegen_output_writer_test_" + std::to_string(UniqueSuffix())))
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
			Expect(stream.good(), "fixture write should succeed");
		}

		[[nodiscard]] std::string Read(std::string_view relative_path) const
		{
			std::ifstream stream(root_ / std::filesystem::path(relative_path), std::ios::binary);
			std::ostringstream buffer;
			buffer << stream.rdbuf();
			Expect(stream.good(), "fixture read should succeed");
			return buffer.str();
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> SampleFiles()
	{
		return {
			mockfakegen::MakeGeneratedFile(
				"MockHoge.h", "mock", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"nested/FakeHoge.cpp", "fake", mockfakegen::GeneratedFileKind::FakeSource),
		};
	}

	void DryRunPlansWithoutCreatingOutputDirectory()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = true, .overwrite = false}, SampleFiles());

		Expect(result.ok(), "dry-run should not diagnose writes");
		Expect(result.files.size() == 2U, "dry-run should report all files");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Planned,
			   "dry-run should mark planned files");
		Expect(!std::filesystem::exists(output_dir), "dry-run should not create output directory");
	}

	void WritesFilesAndCreatesDirectories()
	{
		TempTree tree;
		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			SampleFiles());

		Expect(result.ok(), "normal write should succeed");
		Expect(result.files.size() == 2U, "normal write should report all files");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Written,
			   "first file should be written");
		Expect(result.files[1].status == mockfakegen::OutputWriteStatus::Written,
			   "second file should be written");
		Expect(tree.Read("generated/MockHoge.h") == "mock\n",
			   "mock file content should be written");
		Expect(tree.Read("generated/nested/FakeHoge.cpp") == "fake\n",
			   "nested fake file content should be written");
	}

	void RejectsExistingFileWithoutOverwrite()
	{
		TempTree tree;
		tree.Write("generated/MockHoge.h", "old\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			SampleFiles());

		Expect(!result.ok(), "existing file should fail without overwrite");
		Expect(result.diagnostics.size() == 1U, "existing file should produce one diagnostic");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::SkippedExisting,
			   "existing file should be skipped");
		Expect(tree.Read("generated/MockHoge.h") == "old\n", "existing file should be preserved");
	}

	void OverwritesExistingFileWhenAllowed()
	{
		TempTree tree;
		tree.Write("generated/MockHoge.h", "old\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = true},
			SampleFiles());

		Expect(result.ok(), "overwrite should succeed");
		Expect(tree.Read("generated/MockHoge.h") == "mock\n", "existing file should be replaced");
	}

	void ReportsOutputDirectoryCreationFailure()
	{
		TempTree tree;
		tree.Write("generated", "not a directory\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			SampleFiles());

		Expect(!result.ok(), "file output dir should fail");
		Expect(result.diagnostics.size() == 1U, "output dir failure should produce one diagnostic");
		Expect(result.files.size() == 2U, "output dir failure should mark all files failed");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Failed,
			   "output dir failure should mark file failed");
	}
} // namespace

int main()
{
	DryRunPlansWithoutCreatingOutputDirectory();
	WritesFilesAndCreatesDirectories();
	RejectsExistingFileWithoutOverwrite();
	OverwritesExistingFileWhenAllowed();
	ReportsOutputDirectoryCreationFailure();
	return 0;
}
