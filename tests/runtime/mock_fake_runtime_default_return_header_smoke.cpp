#include <string>

#include <gtest/gtest.h>

#include "MockFakeRuntime.h"

namespace
{
	struct NonDefault
	{
		NonDefault() = delete;
	};

	TEST(MockFakeRuntimeDefaultReturnHeaderSmoke, MissingMockReturnDefaultsValues)
	{
		EXPECT_EQ(mockfake::MissingMockReturn<int>("MockThing::Run()"), 0);
		EXPECT_EQ(mockfake::MissingMockReturn<std::string>("MockThing::Name()"), "");
		mockfake::MissingMockReturn<void>("MockThing::Notify()");
	}

	TEST(MockFakeRuntimeDefaultReturnHeaderSmoke, UnsupportedReturnTypesAbort)
	{
		EXPECT_DEATH(
			{ (void)mockfake::MissingMockReturn<int&>("MockThing::Ref()"); },
			"mockfake: missing mock");
		EXPECT_DEATH(
			{ (void)mockfake::MissingMockReturn<NonDefault>("MockThing::Token()"); },
			"mockfake: missing mock");
	}
} // namespace
