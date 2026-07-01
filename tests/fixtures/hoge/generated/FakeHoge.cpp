#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

bool Hoge::Initialize(int argc, char** argv)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Initialize(std::move(argc), argv);
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Initialize(int, char**)");
}

void Hoge::Finalize()
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		mock->Finalize();
		return;
	}

	return ::mockfake::MissingMockReturn<void>("Hoge::Finalize()");
}

bool Hoge::DoSomething()
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->DoSomething();
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::DoSomething()");
}
