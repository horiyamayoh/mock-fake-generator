#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

int Hoge::GetCount()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->GetCount();
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::GetCount()");
}

bool Hoge::Save(int value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Save(std::move(value));
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Save(int)");
}
