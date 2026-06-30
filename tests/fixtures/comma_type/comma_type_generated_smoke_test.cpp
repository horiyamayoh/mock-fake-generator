#include <map>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Hoge.h"
#include "MockHoge.h"

namespace
{
	using ::testing::Eq;
	using ::testing::Return;

	TEST(CommaTypeGeneratedSmoke, ForwardsCommaContainingTypes)
	{
		MockHoge mock;
		ScopedMockHoge scoped_mock(mock);
		Hoge hoge;

		EXPECT_CALL(mock, GetPair()).WillOnce(Return(std::pair<bool, int>{true, 7}));
		EXPECT_EQ(hoge.GetPair(), (std::pair<bool, int>{true, 7}));

		const std::map<int, double> values{{1, 2.5}, {2, 4.5}};
		EXPECT_CALL(mock, SetMap(Eq(values)));
		hoge.SetMap(values);

		const std::pair<int, std::pair<int, int>> nested{3, {4, 5}};
		EXPECT_CALL(mock, Nest()).WillOnce(Return(nested));
		EXPECT_EQ(hoge.Nest(), nested);
	}
} // namespace
