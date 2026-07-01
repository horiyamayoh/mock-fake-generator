#pragma once

#include <gmock/gmock.h>

#include "GlobalWorker.h"
#include "MockFakeRuntime.h"

class MockGlobalWorker
{
  public:
	MockGlobalWorker() = default;
	~MockGlobalWorker() = default;

	MOCK_METHOD(bool, Run, (), ());
};

using ScopedMockGlobalWorker = ::mockfake::ScopedMock<MockGlobalWorker>;
