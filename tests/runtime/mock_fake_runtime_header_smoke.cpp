#include <cstdlib>
#include <iostream>

#include "MockFakeRuntime.h"

namespace
{
	struct MockThing
	{
		int id = 0;
	};

	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	void ScopedMockPushesAndPops()
	{
		MockThing outer{1};
		MockThing inner{2};

		Expect(mockfake::CurrentMock<MockThing>() == nullptr, "registry should start empty");
		{
			const mockfake::ScopedMock<MockThing> outer_scope(outer);
			Expect(mockfake::CurrentMock<MockThing>() == &outer, "outer mock should be current");
			{
				const mockfake::ScopedMock<MockThing> inner_scope(inner);
				Expect(mockfake::CurrentMock<MockThing>() == &inner,
					   "inner mock should shadow outer");
			}
			Expect(mockfake::CurrentMock<MockThing>() == &outer, "outer mock should be restored");
		}
		Expect(mockfake::CurrentMock<MockThing>() == nullptr,
			   "registry should be empty after scopes");
	}
} // namespace

int main()
{
	ScopedMockPushesAndPops();
	return 0;
}
