#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MockSpecial.h"
#include "Special.h"

namespace
{
	TEST(SpecialMemberGeneratedSmoke, ConstructorDestructorAndMethodFakeLink)
	{
		Special special(42);
		MockSpecial mock;
		const ScopedMockSpecial scoped(mock);

		EXPECT_CALL(mock, Touch(3)).WillOnce(::testing::Return(true));

		EXPECT_TRUE(special.Touch(3));
	}
} // namespace
