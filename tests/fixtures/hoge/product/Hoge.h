#pragma once

class Hoge
{
  public:
	Hoge() = default;
	~Hoge() = default;

	bool Initialize(int argc, char* argv[]);
	void Finalize();
	bool DoSomething();
};
