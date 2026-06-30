#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Eq;
	using ::testing::Return;
	using ::testing::StrEq;

	TEST(DefaultArgumentGeneratedSmoke, ForwardsExplicitAndDefaultedArguments)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		EXPECT_CALL(mock, Open(Eq(std::string("config.json")), 0)).WillOnce(Return(true));
		EXPECT_TRUE(hoge.Open("config.json"));

		EXPECT_CALL(mock, Retry(3, StrEq("retry-default"))).WillOnce(Return(42));
		EXPECT_EQ(hoge.Retry(), 42);

		EXPECT_CALL(mock, Open(Eq(std::string("explicit.json")), 7)).WillOnce(Return(false));
		EXPECT_FALSE(hoge.Open("explicit.json", 7));
	}
} // namespace
