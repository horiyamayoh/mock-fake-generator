#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

std::pair<bool, int> Hoge::GetPair()
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->GetPair();
	}

	return ::mockfake::MissingMockReturn<std::pair<bool, int>>("Hoge::GetPair()");
}

void Hoge::SetMap(std::map<int, double> value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		mock->SetMap(std::move(value));
		return;
	}

	return ::mockfake::MissingMockReturn<void>("Hoge::SetMap(std::map<int, double>)");
}

std::pair<int, std::pair<int, int>> Hoge::Nest()
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Nest();
	}

	return ::mockfake::MissingMockReturn<std::pair<int, std::pair<int, int>>>("Hoge::Nest()");
}
