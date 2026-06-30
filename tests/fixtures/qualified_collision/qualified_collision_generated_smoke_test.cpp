#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Mock_a_Hoge.h"
#include "Mock_b_Hoge.h"
#include "a/Hoge.h"
#include "b/Hoge.h"

namespace
{
	using ::testing::Return;

	TEST(QualifiedCollisionGeneratedSmoke, ForwardsBothCollidingClasses)
	{
		a::MockHoge a_mock;
		b::MockHoge b_mock;
		a::ScopedMockHoge scoped_a_mock(a_mock);
		b::ScopedMockHoge scoped_b_mock(b_mock);
		a::Hoge a_hoge;
		b::Hoge b_hoge;

		EXPECT_CALL(a_mock, Ping()).WillOnce(Return(true));
		EXPECT_CALL(b_mock, Ping()).WillOnce(Return(false));

		EXPECT_TRUE(a_hoge.Ping());
		EXPECT_FALSE(b_hoge.Ping());
	}
} // namespace
