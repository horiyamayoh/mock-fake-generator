#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

std::pair<bool, int> Hoge::GetPair()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->GetPair();
	}

	return ::mockfake::MissingMockReturn<std::pair<bool, int>>("Hoge::GetPair()");
}

void Hoge::SetMap(std::map<int, double> value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		mockfake_current_mock->SetMap(std::move(value));
		return;
	}

	return ::mockfake::MissingMockReturn<void>("Hoge::SetMap(std::map<int, double>)");
}

std::pair<int, std::pair<int, int>> Hoge::Nest()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Nest();
	}

	return ::mockfake::MissingMockReturn<std::pair<int, std::pair<int, int>>>("Hoge::Nest()");
}
