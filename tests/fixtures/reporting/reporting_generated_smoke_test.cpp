#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AllMocks.h"

namespace
{
	using ::testing::Return;

	TEST(ReportingGeneratedSmoke, GeneratedFakeForwardsToScopedMock)
	{
		sample::MockService mock;
		sample::ScopedMockService scoped(mock);
		sample::Service service;

		EXPECT_CALL(mock, Run(7)).WillOnce(Return(true));
		EXPECT_TRUE(service.Run(7));
	}
} // namespace
