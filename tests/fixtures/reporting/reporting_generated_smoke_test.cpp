#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AllMocks.h"

namespace
{
	using ::testing::Return;

	TEST(ReportingGeneratedSmoke, DiagnosticMockHeaderCompilesWithoutNotReadyFake)
	{
		sample::MockService mock;

		EXPECT_CALL(mock, Run(7)).WillOnce(Return(true));
		EXPECT_TRUE(mock.Run(7));
	}
} // namespace
