#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "MockFakeRuntime.h"

namespace
{
	struct MockThing
	{
		int id = 0;
	};

	TEST(MockFakeRuntimeGlobalMutexHeaderSmoke, ScopedMockPushesAndPops)
	{
		MockThing outer{1};
		MockThing inner{2};

		EXPECT_EQ(mockfake::CurrentMock<MockThing>(), nullptr);
		{
			const mockfake::ScopedMock<MockThing> outer_scope(outer);
			EXPECT_EQ(mockfake::CurrentMock<MockThing>(), &outer);
			{
				const mockfake::ScopedMock<MockThing> inner_scope(inner);
				EXPECT_EQ(mockfake::CurrentMock<MockThing>(), &inner);
			}
			EXPECT_EQ(mockfake::CurrentMock<MockThing>(), &outer);
		}
		EXPECT_EQ(mockfake::CurrentMock<MockThing>(), nullptr);
	}

	TEST(MockFakeRuntimeGlobalMutexHeaderSmoke, DestructionOrderMismatchAborts)
	{
		::testing::FLAGS_gtest_death_test_style = "threadsafe";

		MockThing outer{1};
		MockThing inner{2};
		std::optional<mockfake::ScopedMock<MockThing>> outer_scope;
		std::optional<mockfake::ScopedMock<MockThing>> inner_scope;
		outer_scope.emplace(outer);
		inner_scope.emplace(inner);

		EXPECT_DEATH(outer_scope.reset(), "ScopedMock destruction order mismatch");

		inner_scope.reset();
		outer_scope.reset();
		EXPECT_EQ(mockfake::CurrentMock<MockThing>(), nullptr);
	}

	TEST(MockFakeRuntimeGlobalMutexHeaderSmoke, MissingMockReturnAborts)
	{
		::testing::FLAGS_gtest_death_test_style = "threadsafe";

		EXPECT_DEATH(
			{ (void)mockfake::MissingMockReturn<int>("MockThing::Run()"); },
			"mockfake: missing mock for MockThing::Run");
		EXPECT_DEATH(
			{ mockfake::MissingMockReturn<void>("MockThing::Notify()"); },
			"mockfake: missing mock for MockThing::Notify");
	}

	TEST(MockFakeRuntimeGlobalMutexHeaderSmoke, RegistryIsVisibleAcrossThreads)
	{
		bool worker_saw_main_mock = false;
		bool worker_scope_used_worker_mock = false;
		bool worker_scope_returned_to_main_mock = false;
		{
			MockThing main_mock{1};
			const mockfake::ScopedMock<MockThing> main_scope(main_mock);

			std::thread worker(
				[&]
				{
					worker_saw_main_mock = mockfake::CurrentMock<MockThing>() == &main_mock;

					MockThing worker_mock{2};
					{
						const mockfake::ScopedMock<MockThing> worker_scope(worker_mock);
						worker_scope_used_worker_mock =
							mockfake::CurrentMock<MockThing>() == &worker_mock;
					}

					worker_scope_returned_to_main_mock =
						mockfake::CurrentMock<MockThing>() == &main_mock;
				});
			worker.join();

			EXPECT_EQ(mockfake::CurrentMock<MockThing>(), &main_mock);
		}

		EXPECT_TRUE(worker_saw_main_mock);
		EXPECT_TRUE(worker_scope_used_worker_mock);
		EXPECT_TRUE(worker_scope_returned_to_main_mock);
		EXPECT_EQ(mockfake::CurrentMock<MockThing>(), nullptr);
	}
} // namespace
