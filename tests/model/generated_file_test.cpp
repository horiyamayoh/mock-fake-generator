#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "model/GeneratedFile.h"

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

	void StoresKindPathContentAndSourceClass()
	{
		const auto file = mockfakegen::MakeGeneratedFile("generated/../MockHoge.h",
														 "#pragma once",
														 mockfakegen::GeneratedFileKind::MockHeader,
														 mockfakegen::GeneratedSourceClass{
															 .qualified_name = "app::Hoge",
															 .source_header = "include/app/Hoge.h",
														 });

		Expect(file.relative_path == "MockHoge.h", "relative path should be lexically normalized");
		Expect(file.content == "#pragma once\n", "content should have a trailing LF");
		Expect(file.kind == mockfakegen::GeneratedFileKind::MockHeader, "kind should be stored");
		Expect(file.source_class.has_value(), "source class metadata should be stored");
		Expect(file.source_class->qualified_name == "app::Hoge",
			   "source class name should be stored");
		Expect(file.source_class->source_header == "include/app/Hoge.h",
			   "source header metadata should be stored");
	}

	void ConvertsKindsToStableText()
	{
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::RuntimeHeader) ==
				   "runtime_header",
			   "runtime header kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::MockHeader) == "mock_header",
			   "mock header kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::FakeSource) == "fake_source",
			   "fake source kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::AllMocksHeader) ==
				   "all_mocks_header",
			   "all mocks kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::CMakeFragment) ==
				   "cmake_fragment",
			   "cmake fragment kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::Manifest) == "manifest",
			   "manifest kind text should be stable");
		Expect(mockfakegen::ToString(mockfakegen::GeneratedFileKind::Report) == "report",
			   "report kind text should be stable");
	}

	void SortsGeneratedFilesDeterministically()
	{
		const std::vector<mockfakegen::GeneratedFile> files{
			mockfakegen::MakeGeneratedFile("MockZoo.h",
										   "z",
										   mockfakegen::GeneratedFileKind::MockHeader,
										   mockfakegen::GeneratedSourceClass{
											   .qualified_name = "Zoo", .source_header = "Zoo.h"}),
			mockfakegen::MakeGeneratedFile("FakeAlpha.cpp",
										   "a",
										   mockfakegen::GeneratedFileKind::FakeSource,
										   mockfakegen::GeneratedSourceClass{
											   .qualified_name = "Alpha",
											   .source_header = "Alpha.h",
										   }),
			mockfakegen::MakeGeneratedFile(
				"AllMocks.h", "all", mockfakegen::GeneratedFileKind::AllMocksHeader),
			mockfakegen::MakeGeneratedFile("MockAlpha.h",
										   "m",
										   mockfakegen::GeneratedFileKind::MockHeader,
										   mockfakegen::GeneratedSourceClass{
											   .qualified_name = "Alpha",
											   .source_header = "Alpha.h",
										   }),
		};

		const auto sorted = mockfakegen::SortedGeneratedFiles(files);

		Expect(sorted.size() == 4U, "sorted output should keep all files");
		Expect(sorted[0].relative_path == "AllMocks.h", "AllMocks.h should sort first by path");
		Expect(sorted[1].relative_path == "FakeAlpha.cpp",
			   "FakeAlpha.cpp should sort second by path");
		Expect(sorted[2].relative_path == "MockAlpha.h", "MockAlpha.h should sort third by path");
		Expect(sorted[3].relative_path == "MockZoo.h", "MockZoo.h should sort fourth by path");
	}

	void KeepsAlreadyTerminatedContentStable()
	{
		const auto content = mockfakegen::EnsureTrailingNewline("line\n");
		Expect(content == "line\n", "existing trailing LF should not be duplicated");
	}
} // namespace

int main()
{
	StoresKindPathContentAndSourceClass();
	ConvertsKindsToStableText();
	SortsGeneratedFilesDeterministically();
	KeepsAlreadyTerminatedContentStable();
	return 0;
}
