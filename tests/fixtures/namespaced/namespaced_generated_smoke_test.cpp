#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Return;

	TEST(NamespacedGeneratedSmoke, ForwardsInsideProductNamespace)
	{
		app::core::MockHoge mock;
		app::core::ScopedMockHoge scoped_mock(mock);
		app::core::Hoge hoge;

		EXPECT_CALL(mock, DoSomething()).WillOnce(Return(true));
		EXPECT_TRUE(hoge.DoSomething());
	}
} // namespace
