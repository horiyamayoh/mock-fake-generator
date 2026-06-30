#include "Hoge.h"

bool Hoge::Initialize(int argc, char* argv[])
{
	static_cast<void>(argc);
	static_cast<void>(argv);
	return false;
}

void Hoge::Finalize() {}

bool Hoge::DoSomething()
{
	return false;
}
