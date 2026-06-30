#include "MockSpecial.h"
#include "Special.h"

Special::Special(int value) : value_{}
{
	(void)value;
}

Special::~Special() {}

bool Special::Touch(int delta)
{
	if (auto* mock = ::mockfake::CurrentMock<MockSpecial>())
	{
		return mock->Touch(delta);
	}

	return ::mockfake::MissingMockReturn<bool>("Special::Touch(int)");
}
