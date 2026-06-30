#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD(bool, Open, (const std::string&, int), ());
	MOCK_METHOD(int, Retry, (int, const char*), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
