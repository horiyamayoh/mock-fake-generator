#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "StaticData.h"

class MockStaticData
{
  public:
	MockStaticData() = default;
	~MockStaticData() = default;

	MOCK_METHOD(int, ReadCount, (), ());
};

using ScopedMockStaticData = ::mockfake::ScopedMock<MockStaticData>;
