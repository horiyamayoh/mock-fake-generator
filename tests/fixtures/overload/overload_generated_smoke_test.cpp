#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Matcher;
	using ::testing::Return;
	using ::testing::StrEq;

	TEST(OverloadGeneratedSmoke, ForwardsIntOverload)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		EXPECT_CALL(mock, Get(7)).WillOnce(Return(11));
		EXPECT_EQ(hoge.Get(7), 11);
	}

	TEST(OverloadGeneratedSmoke, ForwardsCStringOverload)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		EXPECT_CALL(mock, Get(Matcher<const char*>(StrEq("key")))).WillOnce(Return(13));
		EXPECT_EQ(hoge.Get("key"), 13);
	}
} // namespace
