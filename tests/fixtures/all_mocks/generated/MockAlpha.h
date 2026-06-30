#pragma once

#include <gmock/gmock.h>

#include "Alpha.h"
#include "MockFakeRuntime.h"

class MockAlpha
{
  public:
	MockAlpha() = default;
	~MockAlpha() = default;

	MOCK_METHOD(int, Get, (), ());
};

using ScopedMockAlpha = ::mockfake::ScopedMock<MockAlpha>;
