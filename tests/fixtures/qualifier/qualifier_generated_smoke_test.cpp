#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Return;

	TEST(QualifierGeneratedSmoke, ForwardsConstNoexceptAndRefQualifiedMethods)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		const Hoge const_hoge;
		Hoge mutable_hoge;

		EXPECT_CALL(mock, Get()).WillOnce(Return(17));
		EXPECT_EQ(const_hoge.Get(), 17);

		static_assert(noexcept(mutable_hoge.Save()));
		EXPECT_CALL(mock, Save()).WillOnce(Return(true));
		EXPECT_TRUE(mutable_hoge.Save());

		EXPECT_CALL(mock, Peek()).WillOnce(Return(23));
		EXPECT_EQ(const_hoge.Peek(), 23);

		EXPECT_CALL(std::move(mock), Take()).WillOnce(Return(std::string("taken")));
		EXPECT_EQ(std::move(mutable_hoge).Take(), "taken");
	}
} // namespace
