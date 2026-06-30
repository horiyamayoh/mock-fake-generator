#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <clang/AST/AST.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/LLVM.h>
#include <clang/Tooling/Tooling.h>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}
} // namespace

int main()
{
	const auto matcher = clang::ast_matchers::cxxRecordDecl();
	static_cast<void>(matcher);

	const std::unique_ptr<clang::ASTUnit> ast =
		clang::tooling::buildASTFromCode("class Hoge { public: bool DoSomething(); };", "Hoge.h");
	Expect(ast != nullptr, "LibTooling should build an AST from code");
	Expect(ast->getDiagnostics().getClient() != nullptr, "AST should expose diagnostics client");

	const std::string absolute = clang::tooling::getAbsolutePath("Hoge.h");
	Expect(!absolute.empty(), "Clang Tooling helper should link and return a path");
	return 0;
}
