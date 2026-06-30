#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

int Hoge::Get() const
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Get();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get() const");
}

bool Hoge::Save() noexcept
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Save();
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Save() noexcept");
}

std::string Hoge::Take() &&
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return std::move(*mock).Take();
	}

	return ::mockfake::MissingMockReturn<std::string>("Hoge::Take() &&");
}

int Hoge::Peek() const&
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Peek();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Peek() const&");
}
