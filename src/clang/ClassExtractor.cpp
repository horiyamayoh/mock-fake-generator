#include "clang/ClassExtractor.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/Casting.h>

#include "clang/TypeSpellingService.h"

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute = std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}

			std::error_code canonical_error;
			const auto canonical = std::filesystem::weakly_canonical(absolute, canonical_error);
			if (!canonical_error)
			{
				return canonical.lexically_normal();
			}

			return absolute.lexically_normal();
		}

		[[nodiscard]] SourceLocation ToSourceLocation(const clang::SourceManager& source_manager,
													  clang::SourceLocation location)
		{
			const auto expansion_location = source_manager.getExpansionLoc(location);
			const auto presumed = source_manager.getPresumedLoc(expansion_location);
			if (presumed.isInvalid())
			{
				return {};
			}

			return SourceLocation{
				.file = presumed.getFilename(),
				.line = presumed.getLine(),
				.column = presumed.getColumn(),
			};
		}

		[[nodiscard]] SourceRange ToSourceRange(const clang::SourceManager& source_manager,
												clang::SourceRange range)
		{
			return SourceRange{
				.begin = ToSourceLocation(source_manager, range.getBegin()),
				.end = ToSourceLocation(source_manager, range.getEnd()),
			};
		}

		[[nodiscard]] std::vector<std::string> NamespaceParts(const clang::DeclContext* context)
		{
			std::vector<std::string> reversed;
			for (const auto* current = context; current != nullptr; current = current->getParent())
			{
				const auto* namespace_decl = llvm::dyn_cast<clang::NamespaceDecl>(current);
				if (namespace_decl == nullptr || namespace_decl->isAnonymousNamespace())
				{
					continue;
				}

				const auto name = namespace_decl->getNameAsString();
				if (!name.empty())
				{
					reversed.push_back(name);
				}
			}

			return std::vector<std::string>(reversed.rbegin(), reversed.rend());
		}

		[[nodiscard]] AccessKind ToAccessKind(clang::AccessSpecifier access)
		{
			switch (access)
			{
				case clang::AS_public:
					return AccessKind::Public;
				case clang::AS_protected:
					return AccessKind::Protected;
				case clang::AS_private:
					return AccessKind::Private;
				case clang::AS_none:
					return AccessKind::Unknown;
			}

			return AccessKind::Unknown;
		}

		[[nodiscard]] RefQualifierKind ToRefQualifierKind(clang::RefQualifierKind qualifier)
		{
			switch (qualifier)
			{
				case clang::RQ_None:
					return RefQualifierKind::None;
				case clang::RQ_LValue:
					return RefQualifierKind::LValue;
				case clang::RQ_RValue:
					return RefQualifierKind::RValue;
			}

			return RefQualifierKind::None;
		}

		[[nodiscard]] bool IsNoexcept(const clang::CXXMethodDecl& method)
		{
			const auto exception_spec = method.getExceptionSpecType();
			return exception_spec == clang::EST_BasicNoexcept ||
				exception_spec == clang::EST_NoexceptTrue || exception_spec == clang::EST_NoThrow;
		}

		[[nodiscard]] bool HasConditionalNoexcept(const clang::CXXMethodDecl& method)
		{
			const auto exception_spec = method.getExceptionSpecType();
			return exception_spec == clang::EST_DependentNoexcept ||
				exception_spec == clang::EST_NoexceptFalse ||
				exception_spec == clang::EST_NoexceptTrue;
		}

		[[nodiscard]] std::string UnsupportedMethodName(const clang::NamedDecl& declaration)
		{
			const auto name = declaration.getNameAsString();
			if (!name.empty())
			{
				return name;
			}

			return declaration.getQualifiedNameAsString();
		}

		[[nodiscard]] UnsupportedItem
		MakeUnsupportedMethod(const clang::NamedDecl& declaration,
							  const clang::SourceManager& source_manager,
							  std::string kind,
							  std::string reason)
		{
			return UnsupportedItem{
				.kind = std::move(kind),
				.name = UnsupportedMethodName(declaration),
				.reason = std::move(reason),
				.suggested_action = "exclude this member or provide a hand-authored mock",
				.source_range = ToSourceRange(source_manager, declaration.getSourceRange()),
			};
		}

		[[nodiscard]] std::string SignatureForReport(const ClassModel& class_model,
													 const clang::CXXMethodDecl& method,
													 const std::vector<ParameterModel>& parameters)
		{
			std::string signature = class_model.qualified_name;
			signature += "::";
			signature += method.getNameAsString();
			signature += '(';
			for (std::size_t index = 0U; index < parameters.size(); ++index)
			{
				if (index != 0U)
				{
					signature += ", ";
				}
				signature += parameters[index].type_spelling;
			}
			signature += ')';
			return signature;
		}

		class ClassExtractorVisitor final : public clang::RecursiveASTVisitor<ClassExtractorVisitor>
		{
		  public:
			ClassExtractorVisitor(const HeaderModel& target_header,
								  const clang::ASTContext& ast_context,
								  const clang::SourceManager& source_manager)
				: target_header_(target_header), ast_context_(ast_context),
				  type_spelling_(ast_context), source_manager_(source_manager),
				  target_path_(AbsoluteNormalized(target_header.absolute_path))
			{
			}

			bool VisitClassTemplateDecl(clang::ClassTemplateDecl* declaration)
			{
				if (declaration == nullptr)
				{
					return true;
				}

				const auto* record = declaration->getTemplatedDecl();
				if (record == nullptr || !IsInTargetHeader(record) || record->isImplicit())
				{
					return true;
				}

				const auto name = declaration->getNameAsString();
				if (name.empty())
				{
					return true;
				}

				result_.unsupported_items.push_back(UnsupportedItem{
					.kind = "class_template",
					.name = name,
					.reason = "class template is not supported by link replacement fake generation",
					.suggested_action = "exclude it or provide a hand-authored mock",
					.source_range = ToSourceRange(source_manager_, declaration->getSourceRange()),
				});
				return true;
			}

			bool VisitCXXRecordDecl(clang::CXXRecordDecl* declaration)
			{
				if (declaration == nullptr || !ShouldExtract(declaration))
				{
					return true;
				}

				const auto namespaces = NamespaceParts(declaration->getDeclContext());
				const auto name = declaration->getNameAsString();
				auto class_model = ClassModel{
					.name = name,
					.qualified_name = BuildQualifiedName(namespaces, name),
					.namespaces = namespaces,
					.mock_name = DefaultMockName(name),
					.mock_header_name = DefaultMockHeaderName(name),
					.fake_source_name = DefaultFakeSourceName(name),
					.source_header = target_header_,
					.mock_methods = {},
					.fake_methods = {},
					.unsupported_items = {},
				};
				ExtractMethods(*declaration, class_model);
				result_.classes.push_back(std::move(class_model));
				return true;
			}

			[[nodiscard]] ClassExtractionResult TakeResult()
			{
				std::stable_sort(result_.classes.begin(),
								 result_.classes.end(),
								 [](const auto& lhs, const auto& rhs)
								 {
									 return lhs.qualified_name < rhs.qualified_name;
								 });
				std::stable_sort(result_.unsupported_items.begin(),
								 result_.unsupported_items.end(),
								 [](const auto& lhs, const auto& rhs)
								 {
									 return lhs.name < rhs.name;
								 });
				return std::move(result_);
			}

		  private:
			[[nodiscard]] bool ShouldExtract(const clang::CXXRecordDecl* declaration) const
			{
				if (declaration->isImplicit() || !declaration->isThisDeclarationADefinition())
				{
					return false;
				}

				if (declaration->getDescribedClassTemplate() != nullptr ||
					llvm::isa<clang::ClassTemplateSpecializationDecl>(declaration))
				{
					return false;
				}

				if (!declaration->isClass())
				{
					return false;
				}

				if (declaration->getIdentifier() == nullptr ||
					declaration->getNameAsString().empty())
				{
					return false;
				}

				return IsInTargetHeader(declaration);
			}

			[[nodiscard]] bool IsInTargetHeader(const clang::Decl* declaration) const
			{
				const auto location = source_manager_.getExpansionLoc(declaration->getLocation());
				if (location.isInvalid() || source_manager_.isInSystemHeader(location))
				{
					return false;
				}

				const auto filename = source_manager_.getFilename(location);
				if (filename.empty())
				{
					return false;
				}

				return AbsoluteNormalized(filename.str()) == target_path_;
			}

			void ExtractMethods(const clang::CXXRecordDecl& declaration, ClassModel& class_model)
			{
				for (const auto* child : declaration.decls())
				{
					const auto* function_template =
						llvm::dyn_cast<clang::FunctionTemplateDecl>(child);
					if (function_template != nullptr)
					{
						const auto* templated = function_template->getTemplatedDecl();
						if (templated != nullptr)
						{
							class_model.unsupported_items.push_back(
								MakeUnsupportedMethod(*templated,
													  source_manager_,
													  "function_template",
													  "function template member is not supported"));
						}
						continue;
					}

					const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(child);
					if (method == nullptr || method->isImplicit())
					{
						continue;
					}

					if (llvm::isa<clang::CXXConstructorDecl>(method))
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "constructor",
												  "constructor fake generation is not supported"));
						continue;
					}
					if (llvm::isa<clang::CXXDestructorDecl>(method))
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "destructor",
												  "destructor fake generation is not supported"));
						continue;
					}
					if (llvm::isa<clang::CXXConversionDecl>(method))
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "conversion_operator",
												  "conversion operator is not supported"));
						continue;
					}
					if (method->isOverloadedOperator())
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "overloaded_operator",
												  "overloaded operator is not supported"));
						continue;
					}
					if (method->getAccess() != clang::AS_public)
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "non_public_method",
												  "only public methods are generated"));
						continue;
					}
					if (method->isDeleted())
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "deleted_method",
												  "deleted method is not supported"));
						continue;
					}
					if (method->isDefaulted())
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "defaulted_method",
												  "defaulted method is not supported"));
						continue;
					}
					if (method->isConstexpr())
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "constexpr_method",
												  "constexpr method is not supported"));
						continue;
					}
					if (method->doesThisDeclarationHaveABody())
					{
						class_model.unsupported_items.push_back(
							MakeUnsupportedMethod(*method,
												  source_manager_,
												  "inline_body",
												  "inline method body is not supported"));
						continue;
					}

					auto method_model = BuildMethodModel(*method, class_model);
					class_model.mock_methods.push_back(method_model);
					class_model.fake_methods.push_back(std::move(method_model));
				}
			}

			[[nodiscard]] MethodModel BuildMethodModel(const clang::CXXMethodDecl& method,
													   const ClassModel& class_model) const
			{
				std::vector<ParameterModel> parameters;
				parameters.reserve(method.parameters().size());
				for (std::size_t index = 0U; index < method.parameters().size(); ++index)
				{
					const auto* parameter = method.parameters()[index];
					parameters.push_back(type_spelling_.SpellParameter(*parameter, index));
				}

				const auto return_type = type_spelling_.SpellType(method.getReturnType());
				return MethodModel{
					.name = method.getNameAsString(),
					.qualified_owner_name = class_model.qualified_name,
					.return_type_spelling = return_type.spelling,
					.gmock_return_type_spelling = return_type.gmock_spelling,
					.parameters = parameters,
					.signature_for_report = SignatureForReport(class_model, method, parameters),
					.is_static = method.isStatic(),
					.is_const = method.isConst(),
					.is_volatile = method.isVolatile(),
					.is_noexcept = IsNoexcept(method),
					.has_conditional_noexcept = HasConditionalNoexcept(method),
					.is_virtual = method.isVirtual(),
					.is_pure_virtual = method.isPureVirtual(),
					.is_inline = method.isInlined(),
					.is_deleted = method.isDeleted(),
					.is_defaulted = method.isDefaulted(),
					.ref_qualifier = ToRefQualifierKind(method.getRefQualifier()),
					.access = ToAccessKind(method.getAccess()),
					.source_range = ToSourceRange(source_manager_, method.getSourceRange()),
				};
			}

			const HeaderModel& target_header_;
			const clang::ASTContext& ast_context_;
			TypeSpellingService type_spelling_;
			const clang::SourceManager& source_manager_;
			std::filesystem::path target_path_;
			ClassExtractionResult result_;
		};
	} // namespace

	ClassExtractionResult ExtractClassDefinitionsFromAst(const clang::ASTUnit& ast,
														 const HeaderModel& target_header)
	{
		ClassExtractorVisitor visitor(target_header, ast.getASTContext(), ast.getSourceManager());
		visitor.TraverseDecl(ast.getASTContext().getTranslationUnitDecl());
		return visitor.TakeResult();
	}
} // namespace mockfakegen
