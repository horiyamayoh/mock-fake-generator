#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MockStaticData.h"
#include "StaticData.h"

namespace
{
	using ::testing::Return;

	TEST(StaticDataGeneratedSmoke, DefinesStaticDataAndForwardsMethods)
	{
		StaticData::count = 12;
		EXPECT_EQ(StaticData::count, 12);
		EXPECT_EQ(StaticData::limit, 0);
		EXPECT_EQ(StaticData::inline_count, 3);
		EXPECT_EQ(StaticData::cached, 9);

		MockStaticData mock;
		ScopedMockStaticData scoped_mock(mock);

		EXPECT_CALL(mock, ReadCount()).WillOnce(Return(12));
		EXPECT_EQ(StaticData::ReadCount(), 12);
	}
} // namespace
