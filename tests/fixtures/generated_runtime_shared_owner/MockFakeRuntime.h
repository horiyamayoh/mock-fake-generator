#pragma once

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
