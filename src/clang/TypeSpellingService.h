#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

#include "model/ProjectModel.h"

namespace clang
{
	class ASTContext;
} // namespace clang

namespace mockfakegen
{
	struct TypeSpelling
	{
		std::string spelling;
		std::string gmock_spelling;
		bool gmock_wrapped = false;
	};

	class TypeSpellingService
	{
	  public:
		explicit TypeSpellingService(const clang::ASTContext& ast_context);

		[[nodiscard]] TypeSpelling SpellType(clang::QualType type) const;
		[[nodiscard]] ParameterModel SpellParameter(const clang::ParmVarDecl& parameter,
													std::size_t parameter_index) const;

		[[nodiscard]] static bool NeedsGMockParens(std::string_view spelling);
		[[nodiscard]] static std::string WrapForGMockIfNeeded(std::string spelling);

	  private:
		[[nodiscard]] std::string PrintType(clang::QualType type) const;

		const clang::ASTContext& ast_context_;
	};
} // namespace mockfakegen
