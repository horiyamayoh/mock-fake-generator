#include <string>

#include <gtest/gtest.h>

#include "MockFakeRuntime.h"

namespace
{
	TEST(MockFakeRuntimeDefaultReturnHeaderSmoke, MissingMockReturnDefaultsValues)
	{
		EXPECT_EQ(mockfake::MissingMockReturn<int>("MockThing::Run()"), 0);
		EXPECT_EQ(mockfake::MissingMockReturn<std::string>("MockThing::Name()"), "");
		mockfake::MissingMockReturn<void>("MockThing::Notify()");
	}
} // namespace
