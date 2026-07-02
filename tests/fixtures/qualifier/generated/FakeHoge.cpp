#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

int Hoge::Get() const
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return static_cast<const MockHoge&>(*mockfake_current_mock).Get();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get() const");
}

bool Hoge::Save() noexcept
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Save();
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Save() noexcept");
}

std::string Hoge::Take() &&
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return std::move(*mockfake_current_mock).Take();
	}

	return ::mockfake::MissingMockReturn<std::string>("Hoge::Take() &&");
}

int Hoge::Peek() const&
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return static_cast<const MockHoge&>(*mockfake_current_mock).Peek();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Peek() const&");
}
