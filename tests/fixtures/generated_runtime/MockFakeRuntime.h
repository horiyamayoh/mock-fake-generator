#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace mockfake
{
	namespace detail
	{
		inline void AbortWithMessage(std::string_view message)
		{
			(void)std::fwrite(message.data(), 1U, message.size(), stderr);
			(void)std::fputc('\n', stderr);
			std::abort();
		}

		inline void AbortMissingMock(std::string_view function_name)
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
