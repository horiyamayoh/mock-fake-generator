#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

bool Hoge::Initialize(int argc, char** argv)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->Initialize(std::move(argc), argv);
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Initialize(int, char**)");
}

void Hoge::Finalize()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		mockfake_current_mock->Finalize();
		return;
	}

	return ::mockfake::MissingMockReturn<void>("Hoge::Finalize()");
}

bool Hoge::DoSomething()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mockfake_current_mock->DoSomething();
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::DoSomething()");
}
