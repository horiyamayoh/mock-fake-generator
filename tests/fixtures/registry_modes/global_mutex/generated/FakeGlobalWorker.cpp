#include "GlobalWorker.h"
#include "MockGlobalWorker.h"

bool GlobalWorker::Run()
{
	if (auto* mock = ::mockfake::CurrentMock<MockGlobalWorker>())
	{
		return mock->Run();
	}

	return ::mockfake::MissingMockReturn<bool>("GlobalWorker::Run()");
}
