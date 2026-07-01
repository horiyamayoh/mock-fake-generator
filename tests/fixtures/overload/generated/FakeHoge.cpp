#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

int Hoge::Get(int value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Get(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get(int)");
}

int Hoge::Get(const char* text)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Get(text);
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get(const char*)");
}
