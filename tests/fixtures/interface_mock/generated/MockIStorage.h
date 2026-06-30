#pragma once

#include <gmock/gmock.h>

#include "IStorage.h"

namespace sample
{

	class MockIStorage : public IStorage
	{
	  public:
		MockIStorage() = default;
		~MockIStorage() override = default;

		MOCK_METHOD(bool, Save, (const std::string&, std::string), (override));
		MOCK_METHOD(int, LoadCount, (), (const, noexcept, override));
	};

} // namespace sample
