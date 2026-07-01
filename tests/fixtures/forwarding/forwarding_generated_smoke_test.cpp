#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Forwarding.h"
#include "MockForwarding.h"

namespace
{
	using ::testing::_;
	using ::testing::Ref;
	using ::testing::Return;

	TEST(ForwardingGeneratedSmoke, DispatchesConstAndNonConstOverloads)
	{
		MockForwarding mock;
		const MockForwarding& const_mock = mock;
		ScopedMockForwarding scoped_mock(mock);
		Forwarding forwarding;
		const Forwarding const_forwarding;

		EXPECT_CALL(mock, Select()).WillOnce(Return(11));
		EXPECT_CALL(const_mock, Select()).WillOnce(Return(17));

		EXPECT_EQ(forwarding.Select(), 11);
		EXPECT_EQ(const_forwarding.Select(), 17);
	}

	TEST(ForwardingGeneratedSmoke, DispatchesRefQualifiedOverloads)
	{
		MockForwarding mock;
		const MockForwarding& const_mock = mock;
		ScopedMockForwarding scoped_mock(mock);
		Forwarding forwarding;
		const Forwarding const_forwarding;

		EXPECT_CALL(mock, RefSelect()).WillOnce(Return(23));
		EXPECT_CALL(std::move(mock), RefSelect()).WillOnce(Return(29));
		EXPECT_CALL(const_mock, ConstRefSelect()).WillOnce(Return(31));

		EXPECT_EQ(forwarding.RefSelect(), 23);
		EXPECT_EQ(std::move(forwarding).RefSelect(), 29);
		EXPECT_EQ(const_forwarding.ConstRefSelect(), 31);
	}

	TEST(ForwardingGeneratedSmoke, MovesRvalueAndNonConstByValueParameters)
	{
		MockForwarding mock;
		ScopedMockForwarding scoped_mock(mock);
		Forwarding forwarding;

		EXPECT_CALL(mock, MoveUnique(_))
			.WillOnce(
				[](std::unique_ptr<int> value)
				{
					return *value;
				});
		EXPECT_EQ(forwarding.MoveUnique(std::make_unique<int>(37)), 37);

		EXPECT_CALL(mock, MoveString(_))
			.WillOnce(
				[](std::string value)
				{
					return static_cast<int>(value.size());
				});
		EXPECT_EQ(forwarding.MoveString("moved"), 5);

		EXPECT_CALL(mock, MoveRValue(_))
			.WillOnce(
				[](std::string&& value)
				{
					return static_cast<int>(value.size());
				});
		std::string text = "rvalue";
		EXPECT_EQ(forwarding.MoveRValue(std::move(text)), 6);
	}

	TEST(ForwardingGeneratedSmoke, KeepsNonMovedParameterCategories)
	{
		MockForwarding mock;
		ScopedMockForwarding scoped_mock(mock);
		Forwarding forwarding;
		int number = 41;
		std::string text = "stable";
		int pointed = 53;

		EXPECT_CALL(mock, KeepLValue(Ref(number)))
			.WillOnce(
				[](int& value)
				{
					++value;
					return value;
				});
		EXPECT_EQ(forwarding.KeepLValue(number), 42);
		EXPECT_EQ(number, 42);

		EXPECT_CALL(mock, KeepConstRef(Ref(text))).WillOnce(Return(6));
		EXPECT_EQ(forwarding.KeepConstRef(text), 6);

		EXPECT_CALL(mock, KeepPointer(&pointed)).WillOnce(Return(53));
		EXPECT_EQ(forwarding.KeepPointer(&pointed), 53);

		EXPECT_CALL(mock, KeepConstValue(std::string("const"))).WillOnce(Return(5));
		EXPECT_EQ(forwarding.KeepConstValue("const"), 5);
	}
} // namespace
