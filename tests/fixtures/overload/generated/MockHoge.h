#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD(int, Get, (int), ());
	MOCK_METHOD(int, Get, (const char*), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
