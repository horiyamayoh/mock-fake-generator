#pragma once

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
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
		struct Entry
		{
			Mock* mock;
			const void* scope_token;
		};

	  public:
		static void Push(Mock& mock, const void* scope_token)
		{
			Stack().push_back(Entry{&mock, scope_token});
		}

		static void Pop(const void* scope_token) noexcept
		{
			auto& stack = Stack();
			if (stack.empty() || stack.back().scope_token != scope_token)
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
			return stack.back().mock;
		}

	  private:
		[[nodiscard]] static std::vector<Entry>& Stack()
		{
			thread_local std::vector<Entry> stack;
			return stack;
		}
	};

	template <typename Mock>
	class ScopedMock
	{
	  public:
		explicit ScopedMock(Mock& mock)
		{
			MockRegistry<Mock>::Push(mock, this);
		}

		ScopedMock(const ScopedMock&) = delete;
		ScopedMock& operator=(const ScopedMock&) = delete;

		ScopedMock(ScopedMock&&) = delete;
		ScopedMock& operator=(ScopedMock&&) = delete;

		~ScopedMock() noexcept
		{
			MockRegistry<Mock>::Pop(this);
		}
	};

	template <typename Mock>
	[[nodiscard]] Mock* CurrentMock() noexcept
	{
		return MockRegistry<Mock>::Current();
	}

	template <typename R>
	[[noreturn]] R MissingMockReturn(std::string_view function_name = {})
	{
		if (!function_name.empty())
		{
			throw std::runtime_error("mockfake: missing mock for " + std::string(function_name));
		}
		throw std::runtime_error("mockfake: missing mock");
	}
} // namespace mockfake
