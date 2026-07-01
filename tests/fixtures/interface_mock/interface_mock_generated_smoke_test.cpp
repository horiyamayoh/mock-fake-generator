#include <gmock/gmock.h>
#include <gtest/gtest.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include "IStorage.h"
#include "MockIStorage.h"
#include "MockImplicitDtorIface.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace
{
	using ::testing::Return;

	TEST(InterfaceMockGeneratedSmoke, InheritsAndMocksPureVirtualMethods)
	{
		sample::MockIStorage mock;
		sample::IStorage& storage = mock;

		EXPECT_CALL(mock, Save("key", "value")).WillOnce(Return(true));
		EXPECT_TRUE(storage.Save("key", "value"));

		EXPECT_CALL(mock, LoadCount()).WillOnce(Return(5));
		EXPECT_EQ(storage.LoadCount(), 5);
	}

	TEST(InterfaceMockGeneratedSmoke, OmitsOverrideForImplicitNonVirtualDestructor)
	{
		sample::MockImplicitDtorIface mock;
		sample::ImplicitDtorIface& iface = mock;

		EXPECT_CALL(mock, Run()).WillOnce(Return(7));
		EXPECT_EQ(iface.Run(), 7);
	}
} // namespace
