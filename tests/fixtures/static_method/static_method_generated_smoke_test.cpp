#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Return;

	TEST(StaticMethodGeneratedSmoke, StaticAndInstanceMethodsForwardToRegisteredMock)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		EXPECT_CALL(mock, GetCount()).WillOnce(Return(7));
		EXPECT_EQ(Hoge::GetCount(), 7);

		EXPECT_CALL(mock, Save(3)).WillOnce(Return(true));
		EXPECT_TRUE(hoge.Save(3));
	}
} // namespace
