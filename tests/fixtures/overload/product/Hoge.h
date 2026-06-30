#pragma once

class Hoge
{
  public:
	int Get(int value);
	int Get(const char* text);

	Hoge& operator+=(int value);
};
