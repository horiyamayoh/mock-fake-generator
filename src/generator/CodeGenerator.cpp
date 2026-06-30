#include "generator/CodeGenerator.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "runtime_template/RuntimeTemplate.h"

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::string MockClassName(const SimpleClassModel& class_model)
		{
			return "Mock" + class_model.name;
		}

		[[nodiscard]] std::string ScopedMockAliasName(const SimpleClassModel& class_model)
		{
			return "ScopedMock" + class_model.name;
		}

		[[nodiscard]] std::string NamespaceName(const std::vector<std::string>& namespaces)
		{
			std::string text;
			for (const auto& namespace_part : namespaces)
			{
				if (!text.empty())
				{
					text += "::";
				}
				text += namespace_part;
			}
			return text;
		}

		[[nodiscard]] std::string QualifiedClassName(const SimpleClassModel& class_model)
		{
			const auto namespace_name = NamespaceName(class_model.namespaces);
			if (namespace_name.empty())
			{
				return class_model.name;
			}
			return namespace_name + "::" + class_model.name;
		}

		void OpenNamespace(std::ostringstream& out, const SimpleClassModel& class_model)
		{
			const auto namespace_name = NamespaceName(class_model.namespaces);
			if (!namespace_name.empty())
			{
				out << "namespace " << namespace_name << "\n"
					<< "{\n\n";
			}
		}

		void CloseNamespace(std::ostringstream& out, const SimpleClassModel& class_model)
		{
			const auto namespace_name = NamespaceName(class_model.namespaces);
			if (!namespace_name.empty())
			{
				out << "\n} // namespace " << namespace_name << "\n";
			}
		}

		[[nodiscard]] std::string LocalIndent(const SimpleClassModel& class_model,
											  const std::size_t level)
		{
			std::string indent;
			if (!class_model.namespaces.empty())
			{
				indent += '\t';
			}
			indent.append(level, '\t');
			return indent;
		}

		[[nodiscard]] std::string
		JoinParameterTypes(const std::vector<SimpleParameterModel>& parameters)
		{
			if (parameters.empty())
			{
				return {};
			}

			std::string text;
			for (std::size_t index = 0U; index < parameters.size(); ++index)
			{
				if (index != 0U)
				{
					text += ", ";
				}
				text += parameters[index].type;
			}
			return text;
		}

		[[nodiscard]] std::string
		JoinParameterDeclarations(const std::vector<SimpleParameterModel>& parameters)
		{
			if (parameters.empty())
			{
				return {};
			}

			std::string text;
			for (std::size_t index = 0U; index < parameters.size(); ++index)
			{
				if (index != 0U)
				{
					text += ", ";
				}
				text += parameters[index].type;
				text += ' ';
				text += parameters[index].name;
			}
			return text;
		}

		[[nodiscard]] std::string
		JoinParameterNames(const std::vector<SimpleParameterModel>& parameters)
		{
			if (parameters.empty())
			{
				return {};
			}

			std::string text;
			for (std::size_t index = 0U; index < parameters.size(); ++index)
			{
				if (index != 0U)
				{
					text += ", ";
				}
				text += parameters[index].name;
			}
			return text;
		}

		[[nodiscard]] std::string MethodQualifiers(const SimpleMethodModel& method)
		{
			std::string text;
			if (method.is_const)
			{
				text += " const";
			}

			switch (method.ref_qualifier)
			{
				case RefQualifierKind::None:
					break;
				case RefQualifierKind::LValue:
					text += method.is_const ? "&" : " &";
					break;
				case RefQualifierKind::RValue:
					text += method.is_const ? "&&" : " &&";
					break;
			}

			if (method.is_noexcept)
			{
				text += " noexcept";
			}
			return text;
		}

		[[nodiscard]] std::string GMockMethodSpecs(const SimpleMethodModel& method)
		{
			std::vector<std::string> specs;
			if (method.is_const)
			{
				specs.push_back("const");
			}
			if (method.is_noexcept)
			{
				specs.push_back("noexcept");
			}

			switch (method.ref_qualifier)
			{
				case RefQualifierKind::None:
					break;
				case RefQualifierKind::LValue:
					specs.push_back("ref(&)");
					break;
				case RefQualifierKind::RValue:
					specs.push_back("ref(&&)");
					break;
			}

			std::string text;
			for (std::size_t index = 0U; index < specs.size(); ++index)
			{
				if (index != 0U)
				{
					text += ", ";
				}
				text += specs[index];
			}
			return text;
		}

		[[nodiscard]] bool NeedsUtilityInclude(const SimpleClassModel& class_model)
		{
			for (const auto& method : class_model.methods)
			{
				if (method.ref_qualifier == RefQualifierKind::RValue)
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::string MockCallExpression(const SimpleMethodModel& method,
													 const std::string& parameter_names)
		{
			std::string text;
			if (method.ref_qualifier == RefQualifierKind::RValue)
			{
				text += "std::move(*mock).";
			}
			else
			{
				text += "mock->";
			}
			text += method.name;
			text += '(';
			text += parameter_names;
			text += ')';
			return text;
		}

		[[nodiscard]] std::string DiagnosticSignature(const SimpleClassModel& class_model,
													  const SimpleMethodModel& method)
		{
			std::string text = QualifiedClassName(class_model);
			text += "::";
			text += method.name;
			text += '(';
			text += JoinParameterTypes(method.parameters);
			text += ')';
			text += MethodQualifiers(method);
			return text;
		}

		[[nodiscard]] std::string BuildMockHeaderContent(const SimpleClassModel& class_model)
		{
			const auto mock_class_name = MockClassName(class_model);
			std::ostringstream out;
			out << "#pragma once\n\n"
				<< "#include <gmock/gmock.h>\n\n"
				<< "#include \"" << class_model.header_include << "\"\n"
				<< "#include \"MockFakeRuntime.h\"\n\n";

			OpenNamespace(out, class_model);

			const auto indent = LocalIndent(class_model, 0U);
			const auto member_indent = LocalIndent(class_model, 1U);
			out << indent << "class " << mock_class_name << "\n"
				<< indent << "{\n"
				<< indent << "  public:\n"
				<< member_indent << mock_class_name << "() = default;\n"
				<< member_indent << "~" << mock_class_name << "() = default;\n\n";

			for (const auto& method : class_model.methods)
			{
				out << member_indent << "MOCK_METHOD(" << method.return_type << ", " << method.name
					<< ", (" << JoinParameterTypes(method.parameters) << "), ("
					<< GMockMethodSpecs(method) << "));\n";
			}

			out << indent << "};\n\n"
				<< indent << "using " << ScopedMockAliasName(class_model)
				<< " = ::mockfake::ScopedMock<" << mock_class_name << ">;\n";
			CloseNamespace(out, class_model);

			return out.str();
		}

		[[nodiscard]] std::string BuildFakeSourceContent(const SimpleClassModel& class_model)
		{
			const auto mock_class_name = MockClassName(class_model);
			std::ostringstream out;
			if (NeedsUtilityInclude(class_model))
			{
				out << "#include <utility>\n\n";
			}
			out << "#include \"" << class_model.header_include << "\"\n"
				<< "#include \"" << mock_class_name << ".h\"\n\n";

			OpenNamespace(out, class_model);

			const auto indent = LocalIndent(class_model, 0U);
			const auto body_indent = LocalIndent(class_model, 1U);
			const auto nested_body_indent = LocalIndent(class_model, 2U);
			for (const auto& method : class_model.methods)
			{
				const auto parameter_declarations = JoinParameterDeclarations(method.parameters);
				const auto parameter_names = JoinParameterNames(method.parameters);
				const auto mock_call = MockCallExpression(method, parameter_names);
				const auto signature = DiagnosticSignature(class_model, method);
				const auto is_void_return = method.return_type == "void";

				out << indent << method.return_type << ' ' << class_model.name
					<< "::" << method.name << '(' << parameter_declarations << ')'
					<< MethodQualifiers(method) << "\n"
					<< indent << "{\n"
					<< body_indent << "if (auto* mock = ::mockfake::CurrentMock<" << mock_class_name
					<< ">())\n"
					<< body_indent << "{\n";

				if (is_void_return)
				{
					out << nested_body_indent << mock_call << ";\n"
						<< nested_body_indent << "return;\n";
				}
				else
				{
					out << nested_body_indent << "return " << mock_call << ";\n";
				}

				out << body_indent << "}\n\n"
					<< body_indent << "return ::mockfake::MissingMockReturn<" << method.return_type
					<< ">(\"" << signature << "\");\n"
					<< indent << "}\n";

				if (&method != &class_model.methods.back())
				{
					out << '\n';
				}
			}

			CloseNamespace(out, class_model);
			return out.str();
		}

		[[nodiscard]] GeneratedSourceClass SourceClass(const SimpleClassModel& class_model)
		{
			return GeneratedSourceClass{
				.qualified_name = QualifiedClassName(class_model),
				.source_header = class_model.header_include,
				.generated_method_count = class_model.methods.size(),
			};
		}
	} // namespace

	std::vector<GeneratedFile> GenerateMinimalMockFake(const SimpleClassModel& class_model)
	{
		std::vector<GeneratedFile> files;
		files.push_back(MakeGeneratedFile("Mock" + class_model.name + ".h",
										  BuildMockHeaderContent(class_model),
										  GeneratedFileKind::MockHeader,
										  SourceClass(class_model)));
		files.push_back(MakeGeneratedFile("Fake" + class_model.name + ".cpp",
										  BuildFakeSourceContent(class_model),
										  GeneratedFileKind::FakeSource,
										  SourceClass(class_model)));
		files.push_back(MakeThreadLocalRuntimeHeader());
		SortGeneratedFiles(files);
		return files;
	}

	std::vector<GeneratedFile> GenerateMinimalMockFake(const ClassModel& class_model)
	{
		SimpleClassModel simple_class{
			.name = class_model.name,
			.namespaces = class_model.namespaces,
			.header_include = class_model.source_header.include_spelling,
			.methods = {},
		};
		simple_class.methods.reserve(class_model.mock_methods.size());

		for (const auto& method : class_model.mock_methods)
		{
			SimpleMethodModel simple_method{
				.return_type = method.return_type_spelling,
				.name = method.name,
				.parameters = {},
				.is_const = method.is_const,
				.is_noexcept = method.is_noexcept,
				.ref_qualifier = method.ref_qualifier,
			};
			simple_method.parameters.reserve(method.parameters.size());

			for (const auto& parameter : method.parameters)
			{
				simple_method.parameters.push_back(SimpleParameterModel{
					.type = parameter.type_spelling,
					.name = parameter.generated_name,
				});
			}

			simple_class.methods.push_back(std::move(simple_method));
		}

		return GenerateMinimalMockFake(simple_class);
	}
} // namespace mockfakegen
