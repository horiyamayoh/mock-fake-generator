#pragma once

#include <string>

class Hoge
{
  public:
	bool Open(const std::string& path, int flags = 0);
	int Retry(int count = 3, const char* label = "retry-default");
};
