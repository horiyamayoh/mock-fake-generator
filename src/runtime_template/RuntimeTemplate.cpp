#include "runtime_template/RuntimeTemplate.h"

namespace mockfakegen
{
	std::string BuildThreadLocalRuntimeHeaderContent()
	{
		return R"(#pragma once

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
)";
	}

	std::string BuildGlobalMutexRuntimeHeaderContent()
	{
		return R"(#pragma once

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
)";
	}

	std::string BuildSharedOwnerRuntimeHeaderContent()
	{
		return R"(#pragma once

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
	  public:
		static void Push(std::shared_ptr<Mock> mock)
		{
			if (!mock)
			{
				detail::AbortWithMessage("mockfake: ScopedSharedMock received nullptr");
			}
			std::lock_guard<std::mutex> lock(Mutex());
			Stack().push_back(std::move(mock));
		}

		static void Pop(const std::shared_ptr<Mock>& mock) noexcept
		{
			std::lock_guard<std::mutex> lock(Mutex());
			auto& stack = Stack();
			if (stack.empty() || stack.back().get() != mock.get())
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
			return stack.back();
		}

	  private:
		[[nodiscard]] static std::mutex& Mutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		[[nodiscard]] static std::vector<std::shared_ptr<Mock>>& Stack()
		{
			static std::vector<std::shared_ptr<Mock>> stack;
			return stack;
		}
	};

	template <typename Mock>
	class ScopedSharedMock
	{
	  public:
		explicit ScopedSharedMock(std::shared_ptr<Mock> mock) : mock_(std::move(mock))
		{
			if (!mock_)
			{
				detail::AbortWithMessage("mockfake: ScopedSharedMock received nullptr");
			}
			MockRegistry<Mock>::Push(mock_);
		}

		ScopedSharedMock(const ScopedSharedMock&) = delete;
		ScopedSharedMock& operator=(const ScopedSharedMock&) = delete;

		ScopedSharedMock(ScopedSharedMock&&) = delete;
		ScopedSharedMock& operator=(ScopedSharedMock&&) = delete;

		~ScopedSharedMock() noexcept
		{
			MockRegistry<Mock>::Pop(mock_);
		}

	  private:
		std::shared_ptr<Mock> mock_;
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
)";
	}

	GeneratedFile MakeThreadLocalRuntimeHeader()
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildThreadLocalRuntimeHeaderContent(),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeGlobalMutexRuntimeHeader()
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildGlobalMutexRuntimeHeaderContent(),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeSharedOwnerRuntimeHeader()
	{
		return MakeGeneratedFile("MockFakeRuntime.h",
								 BuildSharedOwnerRuntimeHeaderContent(),
								 GeneratedFileKind::RuntimeHeader);
	}

	GeneratedFile MakeRuntimeHeader(RegistryMode registry_mode)
	{
		switch (registry_mode)
		{
			case RegistryMode::ThreadLocal:
				return MakeThreadLocalRuntimeHeader();
			case RegistryMode::GlobalMutex:
				return MakeGlobalMutexRuntimeHeader();
			case RegistryMode::SharedOwner:
				return MakeSharedOwnerRuntimeHeader();
		}

		return MakeThreadLocalRuntimeHeader();
	}
} // namespace mockfakegen
