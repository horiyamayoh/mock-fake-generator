#pragma once

#include <string>

class Hoge
{
  public:
	int Get() const;
	bool Save() noexcept;
	std::string Take() &&;
	int Peek() const&;

	bool Conditional() noexcept(sizeof(int) == 4);
	int Volatile() volatile;
};
