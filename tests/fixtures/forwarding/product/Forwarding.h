#pragma once

#include <memory>
#include <string>

class Forwarding
{
  public:
	int Select();
	int Select() const;
	int RefSelect() &;
	int RefSelect() &&;
	int ConstRefSelect() const&;

	int MoveUnique(std::unique_ptr<int> value);
	int MoveString(std::string value);
	int MoveRValue(std::string&& value);
	int KeepLValue(int& value);
	int KeepConstRef(const std::string& value);
	int KeepPointer(int* value);
	int KeepConstValue(const std::string value);
	int ConstMoveOnly(const std::unique_ptr<int> value);
};
