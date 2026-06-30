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

		[[nodiscard]] bool HasAccessibleDefaultConstructor(const clang::CXXRecordDecl& record)
		{
			if (!record.hasDefinition())
			{
				return false;
			}

			for (const auto* constructor : record.ctors())
			{
				if (constructor->isDefaultConstructor() && !constructor->isDeleted() &&
					constructor->getAccess() == clang::AS_public)
				{
					return true;
				}
			}

			return record.needsImplicitDefaultConstructor();
		}

		[[nodiscard]] bool IsDefaultConstructibleReturn(clang::QualType type)
		{
			if (type->isVoidType() || type->isReferenceType())
			{
				return false;
			}
			if (type->isBuiltinType() || type->isPointerType() || type->isEnumeralType())
			{
				return true;
			}

			const auto* record = type->getAsCXXRecordDecl();
			if (record == nullptr)
			{
				return false;
			}

			return HasAccessibleDefaultConstructor(*record);
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
							  UnsupportedReasonCode reason_code,
							  std::string class_name,
							  std::string kind,
							  std::string reason)
		{
			auto member_signature = class_name;
			if (!member_signature.empty())
			{
				member_signature += "::";
			}
			member_signature += UnsupportedMethodName(declaration);
			return UnsupportedItem{
				.reason_code = reason_code,
				.kind = std::move(kind),
				.class_name = std::move(class_name),
				.name = UnsupportedMethodName(declaration),
				.member_signature = std::move(member_signature),
				.reason = std::move(reason),
				.suggested_action = "exclude this member or provide a hand-authored mock",
				.source_range = ToSourceRange(source_manager, declaration.getSourceRange()),
			};
		}

		[[nodiscard]] Diagnostic UnsupportedDiagnostic(const UnsupportedItem& item)
		{
			return Diagnostic{
				.severity = DiagnosticSeverity::Warning,
				.code = DiagnosticCode::UnsupportedConstruct,
				.source_range = item.source_range,
				.message = item.kind + ": " + item.reason,
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

				auto item = UnsupportedItem{
					.reason_code = UnsupportedReasonCode::ClassTemplate,
					.kind = "class_template",
					.class_name = name,
					.name = name,
					.member_signature = name,
					.reason = "class template is not supported by link replacement fake generation",
					.suggested_action = "exclude it or provide a hand-authored mock",
					.source_range = ToSourceRange(source_manager_, declaration->getSourceRange()),
				};
				result_.diagnostics.push_back(UnsupportedDiagnostic(item));
				result_.unsupported_items.push_back(std::move(item));
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
							RecordUnsupportedMethod(class_model,
													*templated,
													UnsupportedReasonCode::FunctionTemplate,
													"function_template",
													"function template member is not supported");
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
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::Constructor,
												"constructor",
												"constructor fake generation is not supported");
						continue;
					}
					if (llvm::isa<clang::CXXDestructorDecl>(method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::Destructor,
												"destructor",
												"destructor fake generation is not supported");
						continue;
					}
					if (llvm::isa<clang::CXXConversionDecl>(method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConversionOperator,
												"conversion_operator",
												"conversion operator is not supported");
						continue;
					}
					if (method->isOverloadedOperator())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::OverloadedOperator,
												"overloaded_operator",
												"overloaded operator is not supported");
						continue;
					}
					if (method->getAccess() != clang::AS_public)
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::NonPublicMethod,
												"non_public_method",
												"only public methods are generated");
						continue;
					}
					if (method->isDeleted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DeletedMethod,
												"deleted_method",
												"deleted method is not supported");
						continue;
					}
					if (method->isDefaulted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DefaultedMethod,
												"defaulted_method",
												"defaulted method is not supported");
						continue;
					}
					if (method->isConstexpr())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConstexprMethod,
												"constexpr_method",
												"constexpr method is not supported");
						continue;
					}
					if (method->doesThisDeclarationHaveABody())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::InlineBody,
												"inline_body",
												"inline method body is not supported");
						continue;
					}
					if (HasConditionalNoexcept(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConditionalNoexcept,
												"conditional_noexcept",
												"conditional noexcept is not supported");
						continue;
					}
					if (method->isVolatile())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::VolatileMethod,
												"volatile_method",
												"volatile method is not supported");
						continue;
					}

					auto method_model = BuildMethodModel(*method, class_model);
					class_model.mock_methods.push_back(method_model);
					class_model.fake_methods.push_back(std::move(method_model));
				}
			}

			void RecordUnsupportedMethod(ClassModel& class_model,
										 const clang::NamedDecl& declaration,
										 UnsupportedReasonCode reason_code,
										 std::string kind,
										 std::string reason)
			{
				auto item = MakeUnsupportedMethod(declaration,
												  source_manager_,
												  reason_code,
												  class_model.qualified_name,
												  std::move(kind),
												  std::move(reason));
				result_.diagnostics.push_back(UnsupportedDiagnostic(item));
				class_model.unsupported_items.push_back(std::move(item));
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
				const auto raw_return_type = method.getReturnType();
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
					.return_type_is_void = raw_return_type->isVoidType(),
					.return_type_is_reference = raw_return_type->isReferenceType(),
					.return_type_is_default_constructible =
						IsDefaultConstructibleReturn(raw_return_type),
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
