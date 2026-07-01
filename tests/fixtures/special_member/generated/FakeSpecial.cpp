#include <utility>

#include "MockSpecial.h"
#include "Special.h"

Special::Special(int value) noexcept : value_{}, limit_{}
{
	(void)value;
}

Special::~Special() noexcept {}

bool Special::Touch(int delta)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockSpecial>())
	{
		return mockfake_current_mock->Touch(std::move(delta));
	}

	return ::mockfake::MissingMockReturn<bool>("Special::Touch(int)");
}
