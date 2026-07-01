#include "MockSharedWorker.h"
#include "SharedWorker.h"

int SharedWorker::Run()
{
	if (auto mockfake_current_mock = ::mockfake::CurrentMock<MockSharedWorker>())
	{
		return mockfake_current_mock->Run();
	}

	return ::mockfake::MissingMockReturn<int>("SharedWorker::Run()");
}
