#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "runtime_template/RuntimeTemplate.h"

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

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture should be readable");
		return buffer.str();
	}

	[[nodiscard]] bool Contains(std::string_view text, std::string_view token)
	{
		return text.find(token) != std::string_view::npos;
	}

	void BuildsRuntimeGeneratedFile()
	{
		const auto file = mockfakegen::MakeThreadLocalRuntimeHeader();
		Expect(file.relative_path == "MockFakeRuntime.h", "runtime path should be deterministic");
		Expect(file.kind == mockfakegen::GeneratedFileKind::RuntimeHeader,
			   "runtime generated file should have runtime kind");
		Expect(!file.source_class.has_value(),
			   "runtime generated file should not have source class");
		Expect(Contains(file.content, "template <typename Mock>"),
			   "runtime should define templates");
		Expect(Contains(file.content, "thread_local std::vector<Entry> stack;"),
			   "runtime should use thread-local stack");
		Expect(Contains(file.content, "const void* scope_token"),
			   "runtime should distinguish nested scopes by scope identity");
		Expect(Contains(file.content, "ScopedMock destruction order mismatch"),
			   "runtime should detect destruction order mismatch");
		Expect(Contains(file.content, "MissingMockReturn"),
			   "runtime should define missing mock fallback");
		Expect(!Contains(file.content, "ket::"), "runtime should not contain ket namespace");
		Expect(!Contains(file.content, "#include \"ket_"),
			   "runtime should not include ket quoted headers");
		Expect(!Contains(file.content, "#include <ket_"),
			   "runtime should not include ket angle headers");
	}

	void BuildsGlobalMutexRuntimeGeneratedFile()
	{
		const auto file = mockfakegen::MakeGlobalMutexRuntimeHeader();
		Expect(file.relative_path == "MockFakeRuntime.h",
			   "global runtime path should be deterministic");
		Expect(file.kind == mockfakegen::GeneratedFileKind::RuntimeHeader,
			   "global runtime generated file should have runtime kind");
		Expect(!file.source_class.has_value(),
			   "global runtime generated file should not have source class");
		Expect(Contains(file.content, "#include <mutex>"), "global runtime should include mutex");
		Expect(Contains(file.content, "static std::mutex mutex;"),
			   "global runtime should use a static mutex");
		Expect(Contains(file.content, "static std::vector<Entry> stack;"),
			   "global runtime should use a process-global stack");
		Expect(Contains(file.content, "const void* scope_token"),
			   "global runtime should distinguish nested scopes by scope identity");
		Expect(!Contains(file.content, "thread_local"),
			   "global runtime should not use thread-local storage");
		Expect(!Contains(file.content, "ket::"), "global runtime should not contain ket namespace");
		Expect(!Contains(file.content, "#include \"ket_"),
			   "global runtime should not include ket quoted headers");
		Expect(!Contains(file.content, "#include <ket_"),
			   "global runtime should not include ket angle headers");
	}

	void BuildsSharedOwnerRuntimeGeneratedFile()
	{
		const auto file = mockfakegen::MakeSharedOwnerRuntimeHeader();
		Expect(file.relative_path == "MockFakeRuntime.h",
			   "shared-owner runtime path should be deterministic");
		Expect(file.kind == mockfakegen::GeneratedFileKind::RuntimeHeader,
			   "shared-owner runtime generated file should have runtime kind");
		Expect(!file.source_class.has_value(),
			   "shared-owner runtime generated file should not have source class");
		Expect(Contains(file.content, "#include <memory>"),
			   "shared-owner runtime should include memory");
		Expect(Contains(file.content, "std::shared_ptr<Mock>"),
			   "shared-owner runtime should store shared mock owners");
		Expect(Contains(file.content, "ScopedSharedMock"),
			   "shared-owner runtime should expose shared scope API");
		Expect(!Contains(file.content, "thread_local"),
			   "shared-owner runtime should not use thread-local storage");
		Expect(!Contains(file.content, "ket::"),
			   "shared-owner runtime should not contain ket namespace");
		Expect(!Contains(file.content, "#include \"ket_"),
			   "shared-owner runtime should not include ket quoted headers");
		Expect(!Contains(file.content, "#include <ket_"),
			   "shared-owner runtime should not include ket angle headers");
	}

	void RuntimeModeSelectionUsesGlobalMutex()
	{
		const auto file = mockfakegen::MakeRuntimeHeader(mockfakegen::RegistryMode::GlobalMutex);
		Expect(Contains(file.content, "#include <mutex>"),
			   "runtime mode selection should produce global-mutex runtime");
		Expect(!Contains(file.content, "thread_local"),
			   "runtime mode selection should not produce thread-local runtime");
	}

	void RuntimeModeSelectionUsesSharedOwner()
	{
		const auto file = mockfakegen::MakeRuntimeHeader(mockfakegen::RegistryMode::SharedOwner);
		Expect(Contains(file.content, "ScopedSharedMock"),
			   "runtime mode selection should produce shared-owner runtime");
		Expect(Contains(file.content, "std::shared_ptr<Mock>"),
			   "runtime mode selection should expose shared ownership API");
	}

	void RuntimeFallbackPolicySelectionUsesDefaultReturn()
	{
		const auto file =
			mockfakegen::MakeThreadLocalRuntimeHeader(mockfakegen::FallbackPolicy::DefaultReturn);
		Expect(Contains(file.content, "#include <type_traits>"),
			   "default-return runtime should include type traits");
		Expect(Contains(file.content, "return R{};"),
			   "default-return runtime should default construct values");
		Expect(Contains(file.content, "std::is_reference_v<R>"),
			   "default-return runtime should guard reference returns");
		Expect(Contains(file.content, "std::is_default_constructible_v<R>"),
			   "default-return runtime should guard non-default-constructible returns");
		Expect(Contains(file.content, "detail::AbortMissingMock(function_name);"),
			   "default-return runtime should abort unsupported return types");
		Expect(!Contains(file.content, "[[noreturn]] R MissingMockReturn"),
			   "default-return runtime should not mark fallback noreturn");
	}

	void RuntimeFallbackPolicySelectionUsesThrow()
	{
		const auto file = mockfakegen::MakeRuntimeHeader(mockfakegen::RegistryMode::ThreadLocal,
														 mockfakegen::FallbackPolicy::Throw);
		Expect(Contains(file.content, "#include <stdexcept>"),
			   "throw runtime should include stdexcept");
		Expect(Contains(file.content, "throw std::runtime_error"),
			   "throw runtime should throw runtime_error");
	}

	void MatchesGeneratedFixture()
	{
		const auto expected = ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
									   "tests/fixtures/generated_runtime/MockFakeRuntime.h");
		const auto file = mockfakegen::MakeThreadLocalRuntimeHeader();
		Expect(file.content == expected, "runtime template output should match generated fixture");
	}

	void MatchesGlobalMutexGeneratedFixture()
	{
		const auto expected =
			ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
					 "tests/fixtures/generated_runtime_global_mutex/MockFakeRuntime.h");
		const auto file = mockfakegen::MakeGlobalMutexRuntimeHeader();
		Expect(file.content == expected,
			   "global runtime template output should match generated fixture");
	}

	void MatchesSharedOwnerGeneratedFixture()
	{
		const auto expected =
			ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
					 "tests/fixtures/generated_runtime_shared_owner/MockFakeRuntime.h");
		const auto file = mockfakegen::MakeSharedOwnerRuntimeHeader();
		Expect(file.content == expected,
			   "shared-owner runtime template output should match generated fixture");
	}

	void MatchesDefaultReturnGeneratedFixture()
	{
		const auto expected =
			ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
					 "tests/fixtures/generated_runtime_default_return/MockFakeRuntime.h");
		const auto file =
			mockfakegen::MakeThreadLocalRuntimeHeader(mockfakegen::FallbackPolicy::DefaultReturn);
		Expect(file.content == expected,
			   "default-return runtime template output should match generated fixture");
	}

	void MatchesThrowGeneratedFixture()
	{
		const auto expected = ReadText(std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR) /
									   "tests/fixtures/generated_runtime_throw/MockFakeRuntime.h");
		const auto file =
			mockfakegen::MakeThreadLocalRuntimeHeader(mockfakegen::FallbackPolicy::Throw);
		Expect(file.content == expected,
			   "throw runtime template output should match generated fixture");
	}
} // namespace

int main()
{
	BuildsRuntimeGeneratedFile();
	BuildsGlobalMutexRuntimeGeneratedFile();
	BuildsSharedOwnerRuntimeGeneratedFile();
	RuntimeModeSelectionUsesGlobalMutex();
	RuntimeModeSelectionUsesSharedOwner();
	RuntimeFallbackPolicySelectionUsesDefaultReturn();
	RuntimeFallbackPolicySelectionUsesThrow();
	MatchesGeneratedFixture();
	MatchesGlobalMutexGeneratedFixture();
	MatchesSharedOwnerGeneratedFixture();
	MatchesDefaultReturnGeneratedFixture();
	MatchesThrowGeneratedFixture();
	return 0;
}
