#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "SharedWorker.h"

class MockSharedWorker
{
  public:
	MockSharedWorker() = default;
	~MockSharedWorker() = default;

	MOCK_METHOD(int, Run, (), ());
};

using ScopedMockSharedWorker = ::mockfake::ScopedSharedMock<MockSharedWorker>;
