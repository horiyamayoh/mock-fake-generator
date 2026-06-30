#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "IStorage.h"
#include "MockIStorage.h"

namespace
{
	using ::testing::Return;

	TEST(InterfaceMockGeneratedSmoke, InheritsAndMocksPureVirtualMethods)
	{
		sample::MockIStorage mock;
		sample::IStorage& storage = mock;

		EXPECT_CALL(mock, Save("key", "value")).WillOnce(Return(true));
		EXPECT_TRUE(storage.Save("key", "value"));

		EXPECT_CALL(mock, LoadCount()).WillOnce(Return(5));
		EXPECT_EQ(storage.LoadCount(), 5);
	}
} // namespace
