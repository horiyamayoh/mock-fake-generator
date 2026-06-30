#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Return;

	TEST(HogeGeneratedSmoke, ForwardsCallsThroughScopedMock)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		char arg0[] = "mockfakegen";
		char* argv[] = {arg0, nullptr};

		EXPECT_CALL(mock, Initialize(1, argv)).WillOnce(Return(true));
		EXPECT_CALL(mock, DoSomething()).WillOnce(Return(true));
		EXPECT_CALL(mock, Finalize());

		EXPECT_TRUE(hoge.Initialize(1, argv));
		EXPECT_TRUE(hoge.DoSomething());
		hoge.Finalize();
	}
} // namespace
