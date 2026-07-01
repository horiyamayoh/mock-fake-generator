#include "runtime_template/RuntimeTemplate.h"

#include <string>
#include <string_view>

namespace mockfakegen
{
	namespace
	{
		void ReplaceFirst(std::string& text, std::string_view from, std::string_view to)
		{
			const auto position = text.find(from);
			if (position != std::string::npos)
			{
				text.replace(position, from.size(), to);
			}
		}

		[[nodiscard]] std::string ApplyFallbackPolicy(std::string content,
													  FallbackPolicy fallback_policy)
		{
			constexpr std::string_view abort_missing_mock_return =
				R"(	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		detail::AbortMissingMock(function_name);
	}
)";
			switch (fallback_policy)
			{
				case FallbackPolicy::Abort:
					return content;
				case FallbackPolicy::DefaultReturn:
					ReplaceFirst(content,
								 "#include <string_view>\n",
								 "#include <string_view>\n#include <type_traits>\n");
					ReplaceFirst(content,
								 abort_missing_mock_return,
								 R"(	template <typename R>
	R MissingMockReturn(std::string_view function_name = {})
	{
		(void)function_name;
		if constexpr (std::is_void_v<R>)
		{
			return;
		}
		else
		{
			return R{};
		}
	}
)");
					return content;
				case FallbackPolicy::Throw:
					ReplaceFirst(
						content,
						"#include <string_view>\n",
						"#include <stdexcept>\n#include <string>\n#include <string_view>\n");
					ReplaceFirst(content,
								 abort_missing_mock_return,
								 R"(	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		if (!function_name.empty())
		{
			throw std::runtime_error("mockfake: missing mock for " + std::string(function_name));
		}
		throw std::runtime_error("mockfake: missing mock");
	}
)");
					return content;
			}
			return content;
		}
	} // namespace

	std::string BuildThreadLocalRuntimeHeaderContent(FallbackPolicy fallback_policy)
	{
		return ApplyFallbackPolicy(R"(#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace mockfake
{
	namespace detail
	{
		[[noreturn]] inline void AbortWithMessage(std::string_view message)
		{
			(void)std::fwrite(message.data(), 1U, message.size(), stderr);
			(void)std::fputc('\n', stderr);
			std::abort();
		}

		[[noreturn]] inline void AbortMissingMock(std::string_view function_name)
		{
			(void)std::fputs("mockfake: missing mock", stderr);
			if (!function_name.empty())
			{
				(void)std::fputs(" for ", stderr);
				(void)std::fwrite(function_name.data(), 1U, function_name.size(), stderr);
			}
			(void)std::fputc('\n', stderr);
			std::abort();
		}
	} // namespace detail

	template <typename Mock>
	class MockRegistry
	{
	  public:
		static void Push(Mock& mock)
		{
			Stack().push_back(&mock);
		}

		static void Pop(Mock& mock) noexcept
		{
			auto& stack = Stack();
			if (stack.empty() || stack.back() != &mock)
			{
				detail::AbortWithMessage("mockfake: ScopedMock destruction order mismatch");
			}
			stack.pop_back();
		}

		[[nodiscard]] static Mock* Current() noexcept
		{
			auto& stack = Stack();
			if (stack.empty())
			{
				return nullptr;
			}
			return stack.back();
		}

	  private:
		[[nodiscard]] static std::vector<Mock*>& Stack()
		{
			thread_local std::vector<Mock*> stack;
			return stack;
		}
	};

	template <typename Mock>
	class ScopedMock
	{
	  public:
		explicit ScopedMock(Mock& mock) : mock_(&mock)
		{
			MockRegistry<Mock>::Push(mock);
		}

		ScopedMock(const ScopedMock&) = delete;
		ScopedMock& operator=(const ScopedMock&) = delete;

		ScopedMock(ScopedMock&&) = delete;
		ScopedMock& operator=(ScopedMock&&) = delete;

		~ScopedMock() noexcept
		{
			MockRegistry<Mock>::Pop(*mock_);
		}

	  private:
		Mock* mock_;
	};

	template <typename Mock>
	[[nodiscard]] Mock* CurrentMock() noexcept
	{
		return MockRegistry<Mock>::Current();
	}

	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		detail::AbortMissingMock(function_name);
	}
} // namespace mockfake
)",
								   fallback_policy);
	}

	std::string BuildGlobalMutexRuntimeHeaderContent(FallbackPolicy fallback_policy)
	{
		return ApplyFallbackPolicy(R"(#pragma once

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <vector>

namespace mockfake
{
	namespace detail
	{
		[[noreturn]] inline void AbortWithMessage(std::string_view message)
		{
			(void)std::fwrite(message.data(), 1U, message.size(), stderr);
			(void)std::fputc('\n', stderr);
			std::abort();
		}

		[[noreturn]] inline void AbortMissingMock(std::string_view function_name)
		{
			(void)std::fputs("mockfake: missing mock", stderr);
			if (!function_name.empty())
			{
				(void)std::fputs(" for ", stderr);
				(void)std::fwrite(function_name.data(), 1U, function_name.size(), stderr);
			}
			(void)std::fputc('\n', stderr);
			std::abort();
		}
	} // namespace detail

	template <typename Mock>
	class MockRegistry
	{
	  public:
		static void Push(Mock& mock)
		{
			std::lock_guard<std::mutex> lock(Mutex());
			Stack().push_back(&mock);
		}

		static void Pop(Mock& mock) noexcept
		{
			std::lock_guard<std::mutex> lock(Mutex());
			auto& stack = Stack();
			if (stack.empty() || stack.back() != &mock)
			{
				detail::AbortWithMessage("mockfake: ScopedMock destruction order mismatch");
			}
			stack.pop_back();
		}

		[[nodiscard]] static Mock* Current() noexcept
		{
			std::lock_guard<std::mutex> lock(Mutex());
			auto& stack = Stack();
			if (stack.empty())
			{
				return nullptr;
			}
			return stack.back();
		}

	  private:
		[[nodiscard]] static std::mutex& Mutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		[[nodiscard]] static std::vector<Mock*>& Stack()
		{
			static std::vector<Mock*> stack;
			return stack;
		}
	};

	template <typename Mock>
	class ScopedMock
	{
	  public:
		explicit ScopedMock(Mock& mock) : mock_(&mock)
		{
			MockRegistry<Mock>::Push(mock);
		}

		ScopedMock(const ScopedMock&) = delete;
		ScopedMock& operator=(const ScopedMock&) = delete;

		ScopedMock(ScopedMock&&) = delete;
		ScopedMock& operator=(ScopedMock&&) = delete;

		~ScopedMock() noexcept
		{
			MockRegistry<Mock>::Pop(*mock_);
		}

	  private:
		Mock* mock_;
	};

	template <typename Mock>
	[[nodiscard]] Mock* CurrentMock() noexcept
	{
		return MockRegistry<Mock>::Current();
	}

	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		detail::AbortMissingMock(function_name);
	}
} // namespace mockfake
)",
								   fallback_policy);
	}

	std::string BuildSharedOwnerRuntimeHeaderContent(FallbackPolicy fallback_policy)
	{
		return ApplyFallbackPolicy(R"(#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace mockfake
{
	namespace detail
	{
		[[noreturn]] inline void AbortWithMessage(std::string_view message)
		{
			(void)std::fwrite(message.data(), 1U, message.size(), stderr);
			(void)std::fputc('\n', stderr);
			std::abort();
		}

		[[noreturn]] inline void AbortMissingMock(std::string_view function_name)
		{
			(void)std::fputs("mockfake: missing mock", stderr);
			if (!function_name.empty())
			{
				(void)std::fputs(" for ", stderr);
				(void)std::fwrite(function_name.data(), 1U, function_name.size(), stderr);
			}
			(void)std::fputc('\n', stderr);
			std::abort();
		}
	} // namespace detail

	template <typename Mock>
	class MockRegistry
	{
		struct Entry
		{
			std::shared_ptr<Mock> mock;
			std::shared_ptr<const void> scope_token;
		};

	  public:
		static void Push(std::shared_ptr<Mock> mock, std::shared_ptr<const void> scope_token)
		{
			if (!mock)
			{
				detail::AbortWithMessage("mockfake: ScopedSharedMock received nullptr");
			}
			std::lock_guard<std::mutex> lock(Mutex());
			Stack().push_back(Entry{std::move(mock), std::move(scope_token)});
		}

		static void Pop(const std::shared_ptr<const void>& scope_token) noexcept
		{
			std::lock_guard<std::mutex> lock(Mutex());
			auto& stack = Stack();
			if (stack.empty() || stack.back().scope_token.get() != scope_token.get())
			{
				detail::AbortWithMessage("mockfake: ScopedSharedMock destruction order mismatch");
			}
			stack.pop_back();
		}

		[[nodiscard]] static std::shared_ptr<Mock> Current()
		{
			std::lock_guard<std::mutex> lock(Mutex());
			auto& stack = Stack();
			if (stack.empty())
			{
				return {};
			}
			return stack.back().mock;
		}

	  private:
		[[nodiscard]] static std::mutex& Mutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		[[nodiscard]] static std::vector<Entry>& Stack()
		{
			static std::vector<Entry> stack;
			return stack;
		}
	};

	template <typename Mock>
	class ScopedSharedMock
	{
	  public:
		explicit ScopedSharedMock(std::shared_ptr<Mock> mock)
			: mock_(std::move(mock)), scope_token_(std::make_shared<unsigned char>(0U))
		{
			if (!mock_)
			{
				detail::AbortWithMessage("mockfake: ScopedSharedMock received nullptr");
			}
			MockRegistry<Mock>::Push(mock_, scope_token_);
		}

		ScopedSharedMock(const ScopedSharedMock&) = delete;
		ScopedSharedMock& operator=(const ScopedSharedMock&) = delete;

		ScopedSharedMock(ScopedSharedMock&&) = delete;
		ScopedSharedMock& operator=(ScopedSharedMock&&) = delete;

		~ScopedSharedMock() noexcept
		{
			MockRegistry<Mock>::Pop(scope_token_);
		}

	  private:
		std::shared_ptr<Mock> mock_;
		std::shared_ptr<const void> scope_token_;
	};

	template <typename Mock>
	using ScopedMock = ScopedSharedMock<Mock>;

	template <typename Mock>
	[[nodiscard]] std::shared_ptr<Mock> CurrentMock()
	{
		return MockRegistry<Mock>::Current();
	}

	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		detail::AbortMissingMock(function_name);
	}
} // namespace mockfake
)",
								   fallback_policy);
	}

	GeneratedFile MakeThreadLocalRuntimeHeader(FallbackPolicy fallback_policy)
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildThreadLocalRuntimeHeaderContent(fallback_policy),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeGlobalMutexRuntimeHeader(FallbackPolicy fallback_policy)
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildGlobalMutexRuntimeHeaderContent(fallback_policy),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeSharedOwnerRuntimeHeader(FallbackPolicy fallback_policy)
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildSharedOwnerRuntimeHeaderContent(fallback_policy),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeRuntimeHeader(RegistryMode registry_mode, FallbackPolicy fallback_policy)
	{
		switch (registry_mode)
		{
			case RegistryMode::ThreadLocal:
				return MakeThreadLocalRuntimeHeader(fallback_policy);
			case RegistryMode::GlobalMutex:
				return MakeGlobalMutexRuntimeHeader(fallback_policy);
			case RegistryMode::SharedOwner:
				return MakeSharedOwnerRuntimeHeader(fallback_policy);
		}

		return MakeThreadLocalRuntimeHeader(fallback_policy);
	}
} // namespace mockfakegen
