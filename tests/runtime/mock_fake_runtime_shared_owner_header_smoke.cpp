#include <memory>
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

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, ScopedSharedMockPushesAndPops)
	{
		auto outer = std::make_shared<MockThing>(MockThing{1});
		auto inner = std::make_shared<MockThing>(MockThing{2});

		EXPECT_FALSE(mockfake::CurrentMock<MockThing>());
		{
			const mockfake::ScopedSharedMock<MockThing> outer_scope(outer);
			EXPECT_EQ(mockfake::CurrentMock<MockThing>().get(), outer.get());
			{
				const mockfake::ScopedSharedMock<MockThing> inner_scope(inner);
				EXPECT_EQ(mockfake::CurrentMock<MockThing>().get(), inner.get());
			}
			EXPECT_EQ(mockfake::CurrentMock<MockThing>().get(), outer.get());
		}
		EXPECT_FALSE(mockfake::CurrentMock<MockThing>());
	}

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, DestructionOrderMismatchAborts)
	{
		::testing::FLAGS_gtest_death_test_style = "threadsafe";

		auto outer = std::make_shared<MockThing>(MockThing{1});
		auto inner = std::make_shared<MockThing>(MockThing{2});
		std::optional<mockfake::ScopedSharedMock<MockThing>> outer_scope;
		std::optional<mockfake::ScopedSharedMock<MockThing>> inner_scope;
		outer_scope.emplace(outer);
		inner_scope.emplace(inner);

		EXPECT_DEATH(outer_scope.reset(), "ScopedSharedMock destruction order mismatch");

		inner_scope.reset();
		outer_scope.reset();
		EXPECT_FALSE(mockfake::CurrentMock<MockThing>());
	}

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, NullSharedMockAborts)
	{
		::testing::FLAGS_gtest_death_test_style = "threadsafe";

		EXPECT_DEATH(
			{ const mockfake::ScopedSharedMock<MockThing> scope(std::shared_ptr<MockThing>{}); },
			"ScopedSharedMock received nullptr");
	}

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, MissingMockReturnAborts)
	{
		::testing::FLAGS_gtest_death_test_style = "threadsafe";

		EXPECT_DEATH(
			{ (void)mockfake::MissingMockReturn<int>("MockThing::Run()"); },
			"mockfake: missing mock for MockThing::Run");
		EXPECT_DEATH(
			{ mockfake::MissingMockReturn<void>("MockThing::Notify()"); },
			"mockfake: missing mock for MockThing::Notify");
	}

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, CurrentMockExtendsLifetime)
	{
		std::weak_ptr<MockThing> weak_mock;
		std::shared_ptr<MockThing> current_copy;
		{
			auto mock = std::make_shared<MockThing>(MockThing{7});
			weak_mock = mock;
			const mockfake::ScopedSharedMock<MockThing> scope(mock);
			mock.reset();

			current_copy = mockfake::CurrentMock<MockThing>();
			ASSERT_TRUE(current_copy);
			EXPECT_EQ(current_copy->id, 7);
			EXPECT_FALSE(weak_mock.expired());
		}

		EXPECT_FALSE(mockfake::CurrentMock<MockThing>());
		EXPECT_FALSE(weak_mock.expired());
		current_copy.reset();
		EXPECT_TRUE(weak_mock.expired());
	}

	TEST(MockFakeRuntimeSharedOwnerHeaderSmoke, RegistryIsVisibleAcrossThreads)
	{
		std::weak_ptr<MockThing> weak_main_mock;
		std::shared_ptr<MockThing> worker_copy;
		bool worker_saw_main_mock = false;
		bool worker_scope_used_worker_mock = false;
		bool worker_scope_returned_to_main_mock = false;
		{
			auto main_mock = std::make_shared<MockThing>(MockThing{1});
			weak_main_mock = main_mock;
			const mockfake::ScopedSharedMock<MockThing> main_scope(main_mock);
			main_mock.reset();

			std::thread worker(
				[&]
				{
					worker_copy = mockfake::CurrentMock<MockThing>();
					worker_saw_main_mock =
						worker_copy != nullptr && worker_copy.get() == weak_main_mock.lock().get();

					auto worker_mock = std::make_shared<MockThing>(MockThing{2});
					{
						const mockfake::ScopedSharedMock<MockThing> worker_scope(worker_mock);
						worker_scope_used_worker_mock =
							mockfake::CurrentMock<MockThing>().get() == worker_mock.get();
					}

					worker_scope_returned_to_main_mock =
						mockfake::CurrentMock<MockThing>().get() == worker_copy.get();
				});
			worker.join();

			EXPECT_FALSE(weak_main_mock.expired());
			EXPECT_EQ(mockfake::CurrentMock<MockThing>().get(), worker_copy.get());
		}

		EXPECT_TRUE(worker_saw_main_mock);
		EXPECT_TRUE(worker_scope_used_worker_mock);
		EXPECT_TRUE(worker_scope_returned_to_main_mock);
		EXPECT_FALSE(mockfake::CurrentMock<MockThing>());
		EXPECT_FALSE(weak_main_mock.expired());
		worker_copy.reset();
		EXPECT_TRUE(weak_main_mock.expired());
	}
} // namespace
