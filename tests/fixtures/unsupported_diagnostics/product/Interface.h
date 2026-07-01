#pragma once

namespace negative
{
	class BrokenInterface
	{
	  public:
		explicit BrokenInterface(int value);
		~BrokenInterface();

		virtual bool Save(int value) = 0;
		virtual bool Implemented()
		{
			return true;
		}
		bool NonVirtual();
		static bool StaticCall();
		static int Data;

	  protected:
		virtual bool ProtectedPure() = 0;
	};
} // namespace negative
