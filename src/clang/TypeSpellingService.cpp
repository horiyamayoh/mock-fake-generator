#include "clang/TypeSpellingService.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

namespace mockfakegen
{
	namespace
	{
		void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
		{
			std::size_t position = 0U;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				text.replace(position, from.size(), to);
				position += to.size();
			}
		}

		[[nodiscard]] std::string NormalizePointerAndReferenceSpacing(std::string spelling)
		{
			ReplaceAll(spelling, " *", "*");
			ReplaceAll(spelling, "* ", "*");
			ReplaceAll(spelling, " &", "&");
			ReplaceAll(spelling, "& ", "&");
			return spelling;
		}

		[[nodiscard]] bool IsNonConstByValue(clang::QualType type)
		{
			return !type->isReferenceType() && !type->isPointerType() && !type.isConstQualified();
		}

		[[nodiscard]] std::string JoinTypeAndName(std::string_view type, std::string_view name)
		{
			if (name.empty())
			{
				return std::string(type);
			}
			std::string declaration(type);
			declaration += ' ';
			declaration += name;
			return declaration;
		}

		struct NestedTypeName
		{
			std::string unqualified;
			std::string qualified;
		};

		[[nodiscard]] clang::QualType PeelDeclaratorType(const clang::ASTContext& ast_context,
														 clang::QualType type)
		{
			type = type.getNonReferenceType();
			while (type->isPointerType() || type->isArrayType() || type->isMemberPointerType())
			{
				if (type->isPointerType())
				{
					type = type->getPointeeType();
				}
				else if (const auto* array_type = ast_context.getAsArrayType(type);
						 array_type != nullptr)
				{
					type = array_type->getElementType();
				}
				else if (const auto* member_pointer = type->getAs<clang::MemberPointerType>();
						 member_pointer != nullptr)
				{
					type = member_pointer->getPointeeType();
				}
				type = type.getNonReferenceType();
			}
			return type;
		}

		void AddPublicNestedTypeName(std::vector<NestedTypeName>& names,
									 const clang::NamedDecl* declaration)
		{
			if (declaration == nullptr)
			{
				return;
			}
			const auto* context = declaration->getDeclContext();
			if (context == nullptr || !context->isRecord() ||
				declaration->getAccess() != clang::AS_public)
			{
				return;
			}
			auto unqualified = declaration->getNameAsString();
			auto qualified = declaration->getQualifiedNameAsString();
			if (unqualified.empty() || qualified.empty())
			{
				return;
			}
			for (const auto& name : names)
			{
				if (name.unqualified == unqualified && name.qualified == qualified)
				{
					return;
				}
			}
			names.push_back(NestedTypeName{
				.unqualified = std::move(unqualified),
				.qualified = std::move(qualified),
			});
		}

		void CollectPublicNestedTypeNames(const clang::ASTContext& ast_context,
										  clang::QualType type,
										  std::vector<NestedTypeName>& names,
										  unsigned depth = 0U);

		void CollectPublicNestedTemplateArgumentTypeNames(const clang::ASTContext& ast_context,
														  const clang::TemplateArgument& argument,
														  std::vector<NestedTypeName>& names,
														  unsigned depth)
		{
			if (depth > 64U)
			{
				return;
			}
			switch (argument.getKind())
			{
				case clang::TemplateArgument::Type:
					CollectPublicNestedTypeNames(
						ast_context, argument.getAsType(), names, depth + 1U);
					break;
				case clang::TemplateArgument::Declaration:
					AddPublicNestedTypeName(
						names, llvm::dyn_cast_or_null<clang::NamedDecl>(argument.getAsDecl()));
					break;
				case clang::TemplateArgument::Pack:
					for (const auto& element : argument.pack_elements())
					{
						CollectPublicNestedTemplateArgumentTypeNames(
							ast_context, element, names, depth + 1U);
					}
					break;
				case clang::TemplateArgument::Null:
				case clang::TemplateArgument::NullPtr:
				case clang::TemplateArgument::Integral:
				case clang::TemplateArgument::StructuralValue:
				case clang::TemplateArgument::Template:
				case clang::TemplateArgument::TemplateExpansion:
				case clang::TemplateArgument::Expression:
					break;
			}
		}

		void CollectPublicNestedTypeNames(const clang::ASTContext& ast_context,
										  clang::QualType type,
										  std::vector<NestedTypeName>& names,
										  unsigned depth)
		{
			if (type.isNull() || depth > 64U)
			{
				return;
			}
			type = PeelDeclaratorType(ast_context, type);
			const auto* type_ptr = type.getTypePtrOrNull();
			if (type_ptr == nullptr)
			{
				return;
			}
			if (const auto* typedef_type = llvm::dyn_cast<clang::TypedefType>(type_ptr);
				typedef_type != nullptr)
			{
				AddPublicNestedTypeName(names, typedef_type->getDecl());
				CollectPublicNestedTypeNames(
					ast_context, typedef_type->desugar(), names, depth + 1U);
				return;
			}
			if (const auto* elaborated_type = llvm::dyn_cast<clang::ElaboratedType>(type_ptr);
				elaborated_type != nullptr)
			{
				CollectPublicNestedTypeNames(
					ast_context, elaborated_type->getNamedType(), names, depth + 1U);
				return;
			}
			if (const auto* attributed_type = llvm::dyn_cast<clang::AttributedType>(type_ptr);
				attributed_type != nullptr)
			{
				CollectPublicNestedTypeNames(
					ast_context, attributed_type->getModifiedType(), names, depth + 1U);
				return;
			}
			if (const auto* adjusted_type = llvm::dyn_cast<clang::AdjustedType>(type_ptr);
				adjusted_type != nullptr)
			{
				CollectPublicNestedTypeNames(
					ast_context, adjusted_type->getAdjustedType(), names, depth + 1U);
				return;
			}
			if (const auto* function_type = llvm::dyn_cast<clang::FunctionProtoType>(type_ptr);
				function_type != nullptr)
			{
				CollectPublicNestedTypeNames(
					ast_context, function_type->getReturnType(), names, depth + 1U);
				for (const auto parameter_type : function_type->param_types())
				{
					CollectPublicNestedTypeNames(ast_context, parameter_type, names, depth + 1U);
				}
				return;
			}
			if (const auto* function_type = llvm::dyn_cast<clang::FunctionNoProtoType>(type_ptr);
				function_type != nullptr)
			{
				CollectPublicNestedTypeNames(
					ast_context, function_type->getReturnType(), names, depth + 1U);
				return;
			}
			if (const auto* template_specialization =
					llvm::dyn_cast<clang::TemplateSpecializationType>(type_ptr);
				template_specialization != nullptr)
			{
				for (const auto& argument : template_specialization->template_arguments())
				{
					CollectPublicNestedTemplateArgumentTypeNames(
						ast_context, argument, names, depth + 1U);
				}
			}
			if (const auto* record = type->getAsCXXRecordDecl(); record != nullptr)
			{
				if (const auto* specialization =
						llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
					specialization != nullptr)
				{
					for (const auto& argument : specialization->getTemplateArgs().asArray())
					{
						CollectPublicNestedTemplateArgumentTypeNames(
							ast_context, argument, names, depth + 1U);
					}
				}
				AddPublicNestedTypeName(names, record);
			}
			if (const auto* enum_type = type->getAs<clang::EnumType>(); enum_type != nullptr)
			{
				AddPublicNestedTypeName(names, enum_type->getDecl());
			}
			const auto desugared = type.getSingleStepDesugaredType(ast_context);
			if (desugared != type)
			{
				CollectPublicNestedTypeNames(ast_context, desugared, names, depth + 1U);
			}
		}

		[[nodiscard]] bool IsIdentifierBoundary(char character)
		{
			return !std::isalnum(static_cast<unsigned char>(character)) && character != '_';
		}

		void ReplaceIdentifierToken(std::string& text, std::string_view from, std::string_view to)
		{
			std::size_t position = 0U;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				const auto before_ok = position == 0U || IsIdentifierBoundary(text[position - 1U]);
				const auto after = position + from.size();
				const auto after_ok = after == text.size() || IsIdentifierBoundary(text[after]);
				const auto already_qualified =
					position >= 2U && text[position - 1U] == ':' && text[position - 2U] == ':';
				if (before_ok && after_ok && !already_qualified)
				{
					text.replace(position, from.size(), to);
					position += to.size();
				}
				else
				{
					position = after;
				}
			}
		}

		[[nodiscard]] std::string QualifyPublicNestedType(const clang::ASTContext& ast_context,
														  clang::QualType type,
														  std::string spelling)
		{
			std::vector<NestedTypeName> nested_names;
			CollectPublicNestedTypeNames(ast_context, type, nested_names);
			if (nested_names.empty())
			{
				return spelling;
			}
			std::sort(nested_names.begin(),
					  nested_names.end(),
					  [](const NestedTypeName& lhs, const NestedTypeName& rhs)
					  {
						  return lhs.unqualified.size() > rhs.unqualified.size();
					  });
			for (const auto& nested : nested_names)
			{
				ReplaceIdentifierToken(spelling, nested.unqualified, nested.qualified);
			}
			return spelling;
		}
	} // namespace

	TypeSpellingService::TypeSpellingService(const clang::ASTContext& ast_context)
		: ast_context_(ast_context)
	{
	}

	bool TypeSpellingService::NeedsDeclaratorAwareSpelling(clang::QualType type)
	{
		if (type.isNull())
		{
			return false;
		}
		if (type->isArrayType() || type->isFunctionPointerType() ||
			type->isFunctionReferenceType() || type->isMemberPointerType())
		{
			return true;
		}
		if (type->isReferenceType() || type->isPointerType())
		{
			const auto pointee = type->getPointeeType();
			return !pointee.isNull() && (pointee->isArrayType() || pointee->isFunctionType());
		}
		return false;
	}

	TypeSpelling TypeSpellingService::SpellType(clang::QualType type) const
	{
		auto spelling = QualifyPublicNestedType(ast_context_, type, PrintType(type));
		const auto needs_wrap = NeedsGMockParens(spelling);
		return TypeSpelling{
			.spelling = spelling,
			.gmock_spelling = needs_wrap ? WrapForGMockIfNeeded(std::move(spelling)) : spelling,
			.gmock_wrapped = needs_wrap,
		};
	}

	std::string TypeSpellingService::SpellDeclaration(clang::QualType type,
													  std::string_view name) const
	{
		clang::PrintingPolicy policy(ast_context_.getLangOpts());
		policy.SuppressTagKeyword = true;
		std::string spelling;
		llvm::raw_string_ostream stream(spelling);
		type.print(stream, policy, std::string(name));
		stream.flush();
		return QualifyPublicNestedType(
			ast_context_, type, NormalizePointerAndReferenceSpacing(std::move(spelling)));
	}

	ParameterModel TypeSpellingService::SpellParameter(const clang::ParmVarDecl& parameter,
													   std::size_t parameter_index) const
	{
		const auto type = SpellType(parameter.getType());
		const auto original_name = parameter.getNameAsString();
		const auto generated_name =
			original_name.empty() ? "arg" + std::to_string(parameter_index) : original_name;
		return ParameterModel{
			.type_spelling = type.spelling,
			.gmock_type_spelling = type.gmock_spelling,
			.declaration_spelling = NeedsDeclaratorAwareSpelling(parameter.getType())
				? SpellDeclaration(parameter.getType(), generated_name)
				: JoinTypeAndName(type.spelling, generated_name),
			.original_name = original_name,
			.generated_name = generated_name,
			.has_default_argument = parameter.hasDefaultArg(),
			.is_rvalue_ref = parameter.getType()->isRValueReferenceType(),
			.is_nonconst_by_value = IsNonConstByValue(parameter.getType()),
		};
	}

	bool TypeSpellingService::NeedsGMockParens(std::string_view spelling)
	{
		int paren_depth = 0;
		int bracket_depth = 0;
		for (const char character : spelling)
		{
			switch (character)
			{
				case '(':
					++paren_depth;
					break;
				case ')':
					paren_depth = std::max(0, paren_depth - 1);
					break;
				case '[':
					++bracket_depth;
					break;
				case ']':
					bracket_depth = std::max(0, bracket_depth - 1);
					break;
				case ',':
					if (paren_depth == 0 && bracket_depth == 0)
					{
						return true;
					}
					break;
				default:
					break;
			}
		}
		return false;
	}

	std::string TypeSpellingService::WrapForGMockIfNeeded(std::string spelling)
	{
		if (!NeedsGMockParens(spelling))
		{
			return spelling;
		}
		return '(' + spelling + ')';
	}

	std::string TypeSpellingService::PrintType(clang::QualType type) const
	{
		clang::PrintingPolicy policy(ast_context_.getLangOpts());
		policy.SuppressTagKeyword = true;
		return NormalizePointerAndReferenceSpacing(type.getAsString(policy));
	}
} // namespace mockfakegen
