#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Special.h"

class MockSpecial
{
  public:
	MockSpecial() = default;
	~MockSpecial() = default;

	MOCK_METHOD(bool, Touch, (int), ());
};

using ScopedMockSpecial = ::mockfake::ScopedMock<MockSpecial>;
