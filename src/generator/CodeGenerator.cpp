#include "generator/CodeGenerator.h"

#include <sstream>
#include <string>

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

		[[nodiscard]] std::string DiagnosticSignature(const SimpleClassModel& class_model,
													  const SimpleMethodModel& method)
		{
			std::string text = class_model.name;
			text += "::";
			text += method.name;
			text += '(';
			text += JoinParameterTypes(method.parameters);
			text += ')';
			return text;
		}

		[[nodiscard]] std::string BuildMockHeaderContent(const SimpleClassModel& class_model)
		{
			const auto mock_class_name = MockClassName(class_model);
			std::ostringstream out;
			out << "#pragma once\n\n"
				<< "#include <gmock/gmock.h>\n\n"
				<< "#include \"" << class_model.header_include << "\"\n"
				<< "#include \"MockFakeRuntime.h\"\n\n"
				<< "class " << mock_class_name << "\n"
				<< "{\n"
				<< "public:\n"
				<< "    " << mock_class_name << "() = default;\n"
				<< "    ~" << mock_class_name << "() = default;\n\n";

			for (const auto& method : class_model.methods)
			{
				out << "    MOCK_METHOD(" << method.return_type << ", " << method.name << ", ("
					<< JoinParameterTypes(method.parameters) << "), ());\n";
			}

			out << "};\n\n"
				<< "using " << ScopedMockAliasName(class_model) << " = ::mockfake::ScopedMock<"
				<< mock_class_name << ">;\n";

			return out.str();
		}

		[[nodiscard]] std::string BuildFakeSourceContent(const SimpleClassModel& class_model)
		{
			const auto mock_class_name = MockClassName(class_model);
			std::ostringstream out;
			out << "#include \"" << class_model.header_include << "\"\n"
				<< "#include \"" << mock_class_name << ".h\"\n\n";

			for (const auto& method : class_model.methods)
			{
				const auto parameter_declarations = JoinParameterDeclarations(method.parameters);
				const auto parameter_names = JoinParameterNames(method.parameters);
				const auto signature = DiagnosticSignature(class_model, method);
				const auto is_void_return = method.return_type == "void";

				out << method.return_type << ' ' << class_model.name << "::" << method.name << '('
					<< parameter_declarations << ")\n"
					<< "{\n"
					<< "    if (auto* mock = ::mockfake::CurrentMock<" << mock_class_name
					<< ">())\n"
					<< "    {\n";

				if (is_void_return)
				{
					out << "        mock->" << method.name << '(' << parameter_names << ");\n"
						<< "        return;\n";
				}
				else
				{
					out << "        return mock->" << method.name << '(' << parameter_names
						<< ");\n";
				}

				out << "    }\n\n"
					<< "    return ::mockfake::MissingMockReturn<" << method.return_type << ">(\""
					<< signature << "\");\n"
					<< "}\n";

				if (&method != &class_model.methods.back())
				{
					out << '\n';
				}
			}

			return out.str();
		}

		[[nodiscard]] GeneratedSourceClass SourceClass(const SimpleClassModel& class_model)
		{
			return GeneratedSourceClass{
				.qualified_name = class_model.name,
				.source_header = class_model.header_include,
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
} // namespace mockfakegen
