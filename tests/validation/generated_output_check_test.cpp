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
		Expect(result.diagnostics[0].token == "#include \"ket_",
			   "diagnostic should identify quoted include token");
	}

	void AngleKetIncludeFails()
	{
		TempTree tree;
		tree.Write("MockHoge.h", "#include <ket_file.h>\n");

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "angle ket include should fail");
		Expect(result.diagnostics.size() == 1U, "angle ket include should produce one diagnostic");
		Expect(result.diagnostics[0].token == "#include <ket_",
			   "diagnostic should identify angle include token");
	}

	void EmptyDirectoryFails()
	{
		TempTree tree;

		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({tree.root()});

		Expect(!result.ok(), "empty generated output directory should fail");
		Expect(result.checked_file_count == 0U, "empty directory should count no files");
		Expect(result.diagnostics.size() == 1U, "empty directory should produce one diagnostic");
	}

	void GeneratedFixturesPass()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto result = mockfakegen::CheckGeneratedOutputForKetTokens({
			source_dir / "tests/fixtures/all_mocks/generated",
			source_dir / "tests/fixtures/comma_type/generated",
			source_dir / "tests/fixtures/default_argument/generated",
			source_dir / "tests/fixtures/generated_runtime",
			source_dir / "tests/fixtures/generated_runtime_global_mutex",
			source_dir / "tests/fixtures/generated_runtime_shared_owner",
			source_dir / "tests/fixtures/hoge/generated",
			source_dir / "tests/fixtures/namespaced/generated",
			source_dir / "tests/fixtures/overload/generated",
			source_dir / "tests/fixtures/qualifier/generated",
			source_dir / "tests/fixtures/qualified_collision/generated",
			source_dir / "tests/fixtures/reporting/generated",
			source_dir / "tests/fixtures/special_member/generated",
			source_dir / "tests/fixtures/static_data/generated",
			source_dir / "tests/fixtures/static_method/generated",
		});

		Expect(result.ok(), "generated fixtures should pass ket-token check");
		Expect(result.checked_file_count == 53U, "generated fixtures should be included");
	}
} // namespace

int main()
{
	CleanGeneratedOutputPasses();
	KetNamespaceFails();
	QuotedKetIncludeFails();
	AngleKetIncludeFails();
	EmptyDirectoryFails();
	GeneratedFixturesPass();
	return 0;
}
