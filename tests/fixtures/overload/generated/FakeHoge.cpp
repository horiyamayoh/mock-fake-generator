#include "Hoge.h"
#include "MockHoge.h"

int Hoge::Get(int value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Get(value);
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get(int)");
}

int Hoge::Get(const char* text)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Get(text);
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Get(const char*)");
}
