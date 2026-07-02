#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD(int, Get, (), (const));
	MOCK_METHOD(bool, Save, (), (noexcept));
	MOCK_METHOD(std::string, Take, (), (ref(&&)));
	MOCK_METHOD(int, Peek, (), (const, ref(&)));
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
