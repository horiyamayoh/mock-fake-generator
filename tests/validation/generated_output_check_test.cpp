#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "validation/GeneratedOutputCheck.h"

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
					("mockfakegen_generated_output_check_test_" + std::to_string(UniqueSuffix())))
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
			Expect(stream.good(), "test generated file should be written");
		}

	  private:
		[[nodiscard]] static long long UniqueSuffix()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		std::filesystem::path root_;
	};

	void CleanGeneratedOutputPasses()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#pragma once\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(result.ok(), "clean generated output should pass");
		Expect(result.checked_file_count == 1U, "clean generated output should count files");
	}

	void KetNamespaceFails()
	{
		TempTree tree;
		tree.Write("FakeHoge.cpp", "auto value = ket::string::Cat(\"x\");\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "ket namespace token should fail");
		Expect(result.diagnostics.size() == 1U, "ket namespace should produce one diagnostic");
		Expect(result.diagnostics[0].path.filename() == "FakeHoge.cpp",
			   "diagnostic should identify file");
		Expect(result.diagnostics[0].token == "ket::", "diagnostic should identify token");
	}

	void QuotedKetIncludeFails()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#include \"ket_file.h\"\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "quoted ket include should fail");
		Expect(result.diagnostics.size() == 1U, "quoted ket include should produce one diagnostic");
		Expect(result.diagnostics[0].token == "#include \"ket_file.h\"",
			   "diagnostic should identify quoted include token");
	}

	void AngleKetIncludeFails()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#include <ket_file.h>\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "angle ket include should fail");
		Expect(result.diagnostics.size() == 1U, "angle ket include should produce one diagnostic");
		Expect(result.diagnostics[0].token == "#include <ket_file.h>",
			   "diagnostic should identify angle include token");
	}

	void UnknownKetPrefixedQuotedIncludeFails()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#include \"ket_service.h\"\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "unknown quoted ket_ include should fail");
		Expect(result.diagnostics.size() == 1U,
			   "unknown quoted ket_ include should produce one diagnostic");
		Expect(result.diagnostics[0].token == "#include \"ket_service.h\"",
			   "diagnostic should identify unknown quoted ket_ include token");
	}

	void UnknownKetPrefixedAngleIncludeFails()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#include <ket_service.h>\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "unknown angle ket_ include should fail");
		Expect(result.diagnostics.size() == 1U,
			   "unknown angle ket_ include should produce one diagnostic");
		Expect(result.diagnostics[0].token == "#include <ket_service.h>",
			   "diagnostic should identify unknown angle ket_ include token");
	}

	void SimilarNonKetTokensPass()
	{
		TempTree tree;
		tree.Write("MockPocket.h",
				   "#include \"not_ket_service.h\"\n"
				   "namespace pocket { inline constexpr int value = 1; }\n"
				   "auto x = pocket::value;\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(result.ok(), "similar non-ket tokens should not fail");
		Expect(result.checked_file_count == 1U, "similar token file should be checked");
	}

	void InMemoryGeneratedFilesAreChecked()
	{
		const std::vector files = {
			mockfakegen::MakeGeneratedFile("MockHoge.h",
										   "#pragma once\n"
										   "#include \"ket_file.h\"\n",
										   mockfakegen::GeneratedFileKind::MockHeader),
			mockfakegen::MakeGeneratedFile("FakeHoge.cpp",
										   "auto value = ket::string::Cat(\"x\");\n",
										   mockfakegen::GeneratedFileKind::FakeSource),
		};

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens(files);

		Expect(!result.ok(), "in-memory generated ket tokens should fail");
		Expect(result.checked_file_count == files.size(),
			   "in-memory generated files should be counted");
		Expect(result.diagnostics.size() == 2U,
			   "in-memory generated ket tokens should produce diagnostics");
		Expect(result.diagnostics[0].path == "MockHoge.h" ||
				   result.diagnostics[1].path == "MockHoge.h",
			   "in-memory include diagnostic should identify generated file path");
		Expect(result.diagnostics[0].token == "ket::" || result.diagnostics[1].token == "ket::",
			   "in-memory namespace diagnostic should identify ket token");
	}

	void EmptyDirectoryFails()
	{
		TempTree tree;

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "empty generated output directory should fail");
		Expect(result.checked_file_count == 0U, "empty directory should count no files");
		Expect(result.diagnostics.size() == 1U, "empty directory should produce one diagnostic");
	}

	[[nodiscard]] bool StartsWith(std::string_view text, std::string_view prefix) noexcept
	{
		return text.size() >= prefix.size() && text.substr(0U, prefix.size()) == prefix;
	}

	[[nodiscard]] bool IsGeneratedFixtureRoot(const std::filesystem::path& path)
	{
		const auto name = path.filename().generic_string();
		return name == "generated" || StartsWith(name, "generated_runtime");
	}

	[[nodiscard]] std::vector<std::filesystem::path>
	DiscoverGeneratedFixtureRoots(const std::filesystem::path& source_dir)
	{
		const auto fixtures_dir = source_dir / "tests/fixtures";
		std::vector<std::filesystem::path> roots;

		std::error_code iterator_error;
		auto iterator = std::filesystem::recursive_directory_iterator(fixtures_dir, iterator_error);
		const auto end = std::filesystem::recursive_directory_iterator();
		Expect(!iterator_error, "generated fixture root discovery should start");
		while (iterator != end)
		{
			const auto entry = *iterator;
			std::error_code type_error;
			if (entry.is_directory(type_error) && IsGeneratedFixtureRoot(entry.path()))
			{
				roots.push_back(entry.path());
			}
			Expect(!type_error, "generated fixture root type should be inspectable");

			iterator.increment(iterator_error);
			Expect(!iterator_error, "generated fixture root discovery should continue");
		}

		std::sort(roots.begin(), roots.end());
		return roots;
	}

	[[nodiscard]] bool ContainsPath(const std::vector<std::filesystem::path>& paths,
									const std::filesystem::path& needle)
	{
		return std::find(paths.begin(), paths.end(), needle) != paths.end();
	}

	void GeneratedFixturesPass()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto fixture_roots = DiscoverGeneratedFixtureRoots(source_dir);
		Expect(!fixture_roots.empty(), "generated fixture roots should be discovered");
		Expect(ContainsPath(fixture_roots, source_dir / "tests/fixtures/cxx23_matrix/generated"),
			   "cxx23 matrix generated fixtures should be checked");
		Expect(ContainsPath(fixture_roots, source_dir / "tests/fixtures/forwarding/generated"),
			   "forwarding generated fixtures should be checked");
		Expect(ContainsPath(fixture_roots,
							source_dir / "tests/fixtures/registry_modes/global_mutex/generated"),
			   "global mutex generated fixtures should be checked");
		Expect(ContainsPath(fixture_roots,
							source_dir / "tests/fixtures/registry_modes/shared_owner/generated"),
			   "shared owner generated fixtures should be checked");
		Expect(ContainsPath(fixture_roots,
							source_dir / "tests/fixtures/unsupported_diagnostics/generated"),
			   "unsupported diagnostics generated fixtures should be checked");
		Expect(ContainsPath(fixture_roots,
							source_dir / "tests/fixtures/generated_runtime_default_return"),
			   "default-return runtime fixture should be checked");
		Expect(ContainsPath(fixture_roots, source_dir / "tests/fixtures/generated_runtime_throw"),
			   "throw runtime fixture should be checked");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens(fixture_roots);

		Expect(result.ok(), "generated fixtures should pass ket-token check");
		Expect(result.checked_file_count > fixture_roots.size(),
			   "generated fixture files should be included");
	}
} // namespace

int main()
{
	CleanGeneratedOutputPasses();
	KetNamespaceFails();
	QuotedKetIncludeFails();
	AngleKetIncludeFails();
	UnknownKetPrefixedQuotedIncludeFails();
	UnknownKetPrefixedAngleIncludeFails();
	SimilarNonKetTokensPass();
	InMemoryGeneratedFilesAreChecked();
	EmptyDirectoryFails();
	GeneratedFixturesPass();
	return 0;
}
