#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD(int, GetCount, (), ());
	MOCK_METHOD(bool, Save, (int), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
