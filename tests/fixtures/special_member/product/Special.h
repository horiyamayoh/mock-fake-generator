#pragma once

class Special
{
  public:
	explicit Special(int value);
	~Special();

	bool Touch(int delta);

  private:
	int value_;
};
