#include "MockSharedWorker.h"
#include "SharedWorker.h"

int SharedWorker::Run()
{
	if (auto mock = ::mockfake::CurrentMock<MockSharedWorker>())
	{
		return mock->Run();
	}

	return ::mockfake::MissingMockReturn<int>("SharedWorker::Run()");
}
