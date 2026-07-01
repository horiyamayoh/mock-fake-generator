#include "GlobalWorker.h"
#include "MockGlobalWorker.h"

bool GlobalWorker::Run()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockGlobalWorker>())
	{
		return mockfake_current_mock->Run();
	}

	return ::mockfake::MissingMockReturn<bool>("GlobalWorker::Run()");
}
