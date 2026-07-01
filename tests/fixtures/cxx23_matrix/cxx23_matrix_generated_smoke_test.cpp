#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MockSupported.h"
#include "Supported.h"

namespace
{
	using ::testing::Return;

	TEST(Cxx23MatrixGeneratedSmoke, ForwardsAliasEnumAndStaticCalls)
	{
		app::v1::MockSupported mock;
		app::v1::ScopedMockSupported scoped_mock(mock);
		app::v1::Supported supported;

		EXPECT_CALL(mock, CountItems(app::v1::Mode::Fast)).WillOnce(Return(7));
		EXPECT_CALL(mock, Scale(1.5)).WillOnce(Return(3.0));
		EXPECT_CALL(mock, DefaultMode()).WillOnce(Return(app::v1::Mode::Slow));

		EXPECT_EQ(supported.CountItems(app::v1::Mode::Fast), 7);
		EXPECT_DOUBLE_EQ(supported.Scale(1.5), 3.0);
		EXPECT_EQ(app::v1::Supported::DefaultMode(), app::v1::Mode::Slow);
	}
} // namespace
