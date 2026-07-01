#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

int Hoge::GetCount()
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->GetCount();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::GetCount()");
}

bool Hoge::Save(int value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Save(std::move(value));
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Save(int)");
}
