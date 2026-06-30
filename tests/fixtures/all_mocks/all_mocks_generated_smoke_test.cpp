#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AllMocks.h"

namespace
{
	using ::testing::Return;

	TEST(AllMocksGeneratedSmoke, IncludesEveryGeneratedMock)
	{
		MockAlpha alpha_mock;
		MockBeta beta_mock;
		ScopedMockAlpha scoped_alpha(alpha_mock);
		ScopedMockBeta scoped_beta(beta_mock);
		Alpha alpha;
		Beta beta;

		EXPECT_CALL(alpha_mock, Get()).WillOnce(Return(12));
		EXPECT_EQ(alpha.Get(), 12);

		EXPECT_CALL(beta_mock, Save()).WillOnce(Return(true));
		EXPECT_TRUE(beta.Save());
	}
} // namespace
