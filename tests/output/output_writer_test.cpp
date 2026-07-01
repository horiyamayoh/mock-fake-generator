#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

	void ExpectDiagnosticShape(const mockfakegen::OutputWriteDiagnostic& diagnostic,
							   std::string_view code)
	{
		Expect(diagnostic.severity == mockfakegen::DiagnosticSeverity::Error,
			   "writer diagnostic should carry error severity");
		Expect(diagnostic.code == code, "writer diagnostic should carry expected code");
		Expect(!diagnostic.kind.empty(), "writer diagnostic should carry kind");
		Expect(!diagnostic.message.empty(), "writer diagnostic should carry message");
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

	[[nodiscard]] std::filesystem::path
	StagingRootForOutputDir(const std::filesystem::path& output_dir)
	{
		return output_dir / ".mockfakegen-staging";
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> SingleMockFile()
	{
		return {
			mockfakegen::MakeGeneratedFile(
				"MockHoge.h", "mock", mockfakegen::GeneratedFileKind::MockHeader),
		};
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> SampleFiles()
	{
		return {
			mockfakegen::MakeGeneratedFile(
				"MockHoge.h", "mock", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"nested/FakeHoge.cpp", "fake", mockfakegen::GeneratedFileKind::FakeSource),
		};
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> NewThenConflictingFiles()
	{
		return {
			mockfakegen::MakeGeneratedFile(
				"NewFirst.h", "new", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"MockHoge.h", "mock", mockfakegen::GeneratedFileKind::MockHeader),
		};
	}

	[[nodiscard]] mockfakegen::GeneratedSourceClass SourceClass(std::string name)
	{
		return mockfakegen::GeneratedSourceClass{
			.qualified_name = std::move(name),
			.source_header = "Product.h",
			.generated_method_count = 1U,
			.link_ready = true,
		};
	}

	[[nodiscard]] mockfakegen::GeneratedFile ClassFile(std::filesystem::path relative_path,
													   std::string content,
													   mockfakegen::GeneratedFileKind kind,
													   std::string source_class)
	{
		return mockfakegen::MakeGeneratedFile(std::move(relative_path),
											  std::move(content),
											  kind,
											  SourceClass(std::move(source_class)));
	}

	[[nodiscard]] std::vector<mockfakegen::GeneratedFile> MixedClassFiles()
	{
		return {
			ClassFile(
				"MockGood.h", "good mock", mockfakegen::GeneratedFileKind::MockHeader, "Good"),
			ClassFile(
				"FakeGood.cpp", "good fake", mockfakegen::GeneratedFileKind::FakeSource, "Good"),
			ClassFile(
				"MockOther.h", "other mock", mockfakegen::GeneratedFileKind::MockHeader, "Other"),
			ClassFile(
				"FakeOther.cpp", "other fake", mockfakegen::GeneratedFileKind::FakeSource, "Other"),
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

	void DryRunRejectsUnsafePathsWithoutCreatingOutputDirectory()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const std::vector files = {
			mockfakegen::MakeGeneratedFile("", "empty", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile("/tmp/mockfakegen-escape.h",
										   "absolute",
										   mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"../Escape.h", "parent", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"nested/../../Escape.h", "normalized", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(".mockfakegen-staging/Reserved.h",
										   "reserved",
										   mockfakegen::GeneratedFileKind::MockHeader),
		};

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = true, .overwrite = false}, files);

		Expect(!result.ok(), "dry-run should reject unsafe generated paths");
		Expect(result.diagnostics.size() == files.size(),
			   "all unsafe dry-run paths should diagnose");
		for (const auto& diagnostic : result.diagnostics)
		{
			ExpectDiagnosticShape(diagnostic, "output_path_invalid");
		}
		Expect(!std::filesystem::exists(output_dir),
			   "unsafe dry-run should not create output directory");
		Expect(!std::filesystem::exists(tree.root() / "Escape.h"),
			   "unsafe dry-run should not write outside root");
	}

	void WriteModeRejectsTraversalBeforePublishingAnyFile()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const std::vector files = {
			mockfakegen::MakeGeneratedFile(
				"Safe.h", "safe", mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile(
				"../Escape.h", "escape", mockfakegen::GeneratedFileKind::MockHeader),
		};

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = false, .overwrite = false}, files);

		Expect(!result.ok(), "unsafe write should fail");
		Expect(result.diagnostics.size() == 1U, "unsafe write should diagnose traversal once");
		ExpectDiagnosticShape(result.diagnostics[0], "output_path_invalid");
		Expect(!std::filesystem::exists(output_dir / "Safe.h"),
			   "path validation failure should prevent publishing safe files");
		Expect(!std::filesystem::exists(tree.root() / "Escape.h"),
			   "traversal path should not write outside output directory");
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
		Expect(!std::filesystem::exists(StagingRootForOutputDir(tree.root() / "generated")),
			   "staging directory should not remain after normal write");
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
		ExpectDiagnosticShape(result.diagnostics[0], "output_conflict");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::SkippedExisting,
			   "existing file should be skipped");
		Expect(result.files[1].status == mockfakegen::OutputWriteStatus::Written,
			   "unrelated missing file should still be written");
		Expect(tree.Read("generated/MockHoge.h") == "old\n", "existing file should be preserved");
		Expect(tree.Read("generated/nested/FakeHoge.cpp") == "fake\n",
			   "existing conflict should not prevent partial new file publication");
	}

	void ConflictAfterValidFileKeepsPreviouslyPublishableFile()
	{
		TempTree tree;
		tree.Write("generated/MockHoge.h", "old\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			NewThenConflictingFiles());

		Expect(!result.ok(), "later conflict should fail the generated set");
		Expect(result.diagnostics.size() == 1U, "later conflict should produce one diagnostic");
		ExpectDiagnosticShape(result.diagnostics[0], "output_conflict");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Written,
			   "new unrelated file before conflict should be published");
		Expect(result.files[1].status == mockfakegen::OutputWriteStatus::SkippedExisting,
			   "conflicting file should be skipped");
		Expect(tree.Read("generated/NewFirst.h") == "new\n",
			   "new unrelated file before conflict should appear");
		Expect(tree.Read("generated/MockHoge.h") == "old\n", "conflicting file should be kept");
	}

	void SourceClassConflictSkipsSiblingsAndPublishesOtherClasses()
	{
		TempTree tree;
		tree.Write("generated/MockGood.h", "old\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			MixedClassFiles());

		Expect(!result.ok(), "source-class conflict should diagnose");
		Expect(result.diagnostics.size() == 1U, "source-class conflict should diagnose once");
		ExpectDiagnosticShape(result.diagnostics[0], "output_conflict");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::SkippedExisting,
			   "conflicting class mock should be skipped");
		Expect(result.files[1].status == mockfakegen::OutputWriteStatus::Failed,
			   "same-class fake should not be published after mock conflict");
		Expect(result.files[2].status == mockfakegen::OutputWriteStatus::Written,
			   "other class mock should be written");
		Expect(result.files[3].status == mockfakegen::OutputWriteStatus::Written,
			   "other class fake should be written");
		Expect(tree.Read("generated/MockGood.h") == "old\n", "conflicting mock should be kept");
		Expect(!std::filesystem::exists(tree.root() / "generated" / "FakeGood.cpp"),
			   "same-class fake should not appear");
		Expect(tree.Read("generated/MockOther.h") == "other mock\n",
			   "other class mock should be published");
		Expect(tree.Read("generated/FakeOther.cpp") == "other fake\n",
			   "other class fake should be published");
	}

	void LeavesSameContentUnchanged()
	{
		TempTree tree;
		tree.Write("generated/MockHoge.h", "mock\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = tree.root() / "generated", .dry_run = false, .overwrite = false},
			SampleFiles());

		Expect(result.ok(), "same-content file should not diagnose");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Unchanged,
			   "same-content file should be reported as unchanged");
		Expect(result.files[1].status == mockfakegen::OutputWriteStatus::Written,
			   "missing second file should still be written");
		Expect(tree.Read("generated/MockHoge.h") == "mock\n",
			   "same-content file should be preserved");
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
		Expect(!std::filesystem::exists(StagingRootForOutputDir(tree.root() / "generated")),
			   "staging directory should not remain after overwrite");
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
		ExpectDiagnosticShape(result.diagnostics[0], "output_directory_failure");
		Expect(result.files.size() == 2U, "output dir failure should mark all files failed");
		Expect(result.files[0].status == mockfakegen::OutputWriteStatus::Failed,
			   "output dir failure should mark file failed");
	}

	void RemovesStaleStagingTreeBeforeWriting()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const auto staging_root = StagingRootForOutputDir(output_dir);
		std::filesystem::create_directories(staging_root / "files");
		tree.Write("generated/.mockfakegen-staging/files/stale.tmp", "stale\n");

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = false, .overwrite = false}, SingleMockFile());

		Expect(result.ok(), "stale staging tree should be cleaned before write");
		Expect(tree.Read("generated/MockHoge.h") == "mock\n", "file should be published");
		Expect(!std::filesystem::exists(staging_root),
			   "staging tree should be removed after successful publish");
	}

	void RejectsExistingDirectoryBeforePublish()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const auto output_path = output_dir / "MockHoge.h";
		std::filesystem::create_directories(output_path);

		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = false, .overwrite = true},
			NewThenConflictingFiles());

		Expect(!result.ok(), "existing directory should fail before publish");
		Expect(result.diagnostics.size() == 1U, "existing directory should diagnose once");
		ExpectDiagnosticShape(result.diagnostics[0], "output_conflict");
		Expect(tree.Read("generated/NewFirst.h") == "new\n",
			   "non-regular conflict should not prevent unrelated new file publication");
		Expect(std::filesystem::is_directory(output_path),
			   "non-regular existing path should be preserved");
		Expect(!std::filesystem::exists(StagingRootForOutputDir(output_dir)),
			   "pre-publish conflict should not leave a staging tree");
	}

	void RejectsSymlinkParentEscape()
	{
		TempTree tree;
		const auto output_dir = tree.root() / "generated";
		const auto outside_dir = tree.root() / "outside";
		std::filesystem::create_directories(output_dir);
		std::filesystem::create_directories(outside_dir);
		std::error_code symlink_error;
		std::filesystem::create_directory_symlink(outside_dir, output_dir / "link", symlink_error);
		if (symlink_error)
		{
			return;
		}

		const std::vector files = {
			mockfakegen::MakeGeneratedFile(
				"link/Escape.h", "escape", mockfakegen::GeneratedFileKind::MockHeader),
		};
		const auto result = mockfakegen::WriteGeneratedFiles(
			{.output_dir = output_dir, .dry_run = false, .overwrite = false}, files);

		Expect(!result.ok(), "symlink parent escape should fail");
		Expect(result.diagnostics.size() == 1U, "symlink parent escape should diagnose once");
		ExpectDiagnosticShape(result.diagnostics[0], "output_path_invalid");
		Expect(!std::filesystem::exists(outside_dir / "Escape.h"),
			   "symlink parent should not write outside output directory");
	}
} // namespace

int main()
{
	DryRunPlansWithoutCreatingOutputDirectory();
	DryRunRejectsUnsafePathsWithoutCreatingOutputDirectory();
	WriteModeRejectsTraversalBeforePublishingAnyFile();
	WritesFilesAndCreatesDirectories();
	RejectsExistingFileWithoutOverwrite();
	ConflictAfterValidFileKeepsPreviouslyPublishableFile();
	SourceClassConflictSkipsSiblingsAndPublishesOtherClasses();
	LeavesSameContentUnchanged();
	OverwritesExistingFileWhenAllowed();
	ReportsOutputDirectoryCreationFailure();
	RemovesStaleStagingTreeBeforeWriting();
	RejectsExistingDirectoryBeforePublish();
	RejectsSymlinkParentEscape();
	return 0;
}
