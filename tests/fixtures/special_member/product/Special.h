#pragma once

class SpecialBase
{
  public:
	SpecialBase() = default;
};

class Special : public SpecialBase
{
  public:
	explicit Special(int value) noexcept;
	~Special() noexcept;

	bool Touch(int delta);

  private:
	int value_;
	const int limit_;
	int initialized_ = 7;
};
