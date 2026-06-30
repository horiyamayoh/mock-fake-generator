#pragma once

namespace sample
{
	class Service
	{
	  public:
		bool Run(int value);

		template <class T>
		T Convert(T value);

		Service& operator+=(int delta);
	};
} // namespace sample
