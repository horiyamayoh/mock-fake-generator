#pragma once

#include <gmock/gmock.h>

#include "Beta.h"
#include "MockFakeRuntime.h"

class MockBeta
{
  public:
	MockBeta() = default;
	~MockBeta() = default;

	MOCK_METHOD(bool, Save, (), ());
};

using ScopedMockBeta = ::mockfake::ScopedMock<MockBeta>;
