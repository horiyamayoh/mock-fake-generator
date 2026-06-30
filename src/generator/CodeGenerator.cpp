#include "generator/CodeGenerator.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
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

		[[nodiscard]] std::string MockHeaderFileName(const SimpleClassModel& class_model)
		{
			if (!class_model.mock_header_name.empty())
			{
				return class_model.mock_header_name;
			}
			return "Mock" + class_model.name + ".h";
		}

		[[nodiscard]] std::string FakeSourceFileName(const SimpleClassModel& class_model)
		{
			if (!class_model.fake_source_name.empty())
			{
				return class_model.fake_source_name;
			}
			return "Fake" + class_model.name + ".cpp";
		}

		[[nodiscard]] std::string JsonString(std::string_view value)
		{
			constexpr char hex_digits[] = "0123456789abcdef";
			std::string text;
			text.reserve(value.size() + 2U);
			text += '"';
			for (const char raw_character : value)
			{
				const auto character = static_cast<unsigned char>(raw_character);
				switch (character)
				{
					case '"':
						text += "\\\"";
						break;
					case '\\':
						text += "\\\\";
						break;
					case '\b':
						text += "\\b";
						break;
					case '\f':
						text += "\\f";
						break;
					case '\n':
						text += "\\n";
						break;
					case '\r':
						text += "\\r";
						break;
					case '\t':
						text += "\\t";
						break;
					default:
						if (character < 0x20U)
						{
							text += "\\u00";
							text += hex_digits[(character >> 4U) & 0x0FU];
							text += hex_digits[character & 0x0FU];
						}
						else
						{
							text += static_cast<char>(character);
						}
						break;
				}
			}
			text += '"';
			return text;
		}

		void WriteJsonStringArray(std::ostringstream& out,
								  const std::vector<std::string>& values,
								  std::string_view indent)
		{
			if (values.empty())
			{
				out << "[]";
				return;
			}

			out << "[\n";
			for (std::size_t index = 0U; index < values.size(); ++index)
			{
				out << indent << "  " << JsonString(values[index]);
				if (index + 1U != values.size())
				{
					out << ',';
				}
				out << '\n';
			}
			out << indent << ']';
		}

		[[nodiscard]] std::string MarkdownCell(std::string_view value)
		{
			std::string text;
			text.reserve(value.size());
			for (const char character : value)
			{
				switch (character)
				{
					case '|':
						text += "\\|";
						break;
					case '\n':
					case '\r':
					case '\t':
						text += ' ';
						break;
					default:
						text += character;
						break;
				}
			}
			return text;
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

		[[nodiscard]] std::string QualifiedClassName(const ClassModel& class_model)
		{
			if (!class_model.qualified_name.empty())
			{
				return class_model.qualified_name;
			}
			return BuildQualifiedName(class_model.namespaces, class_model.name);
		}

		[[nodiscard]] std::vector<std::string> LinkReadinessReasons(const ClassModel& class_model)
		{
			std::vector<std::string> reasons = class_model.link_readiness_reasons;
			if (!class_model.unsupported_items.empty())
			{
				reasons.push_back("unsupported items remain");
			}
			if (!class_model.link_ready && reasons.empty())
			{
				reasons.push_back("class marked not link-ready");
			}
			return reasons;
		}

		[[nodiscard]] bool IsLinkReady(const ClassModel& class_model)
		{
			return class_model.link_ready && LinkReadinessReasons(class_model).empty();
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

		[[nodiscard]] std::string SourceHeaderName(const ClassModel& class_model)
		{
			if (!class_model.source_header.include_spelling.empty())
			{
				return class_model.source_header.include_spelling;
			}
			if (!class_model.source_header.project_relative_path.empty())
			{
				return class_model.source_header.project_relative_path.generic_string();
			}
			return class_model.source_header.absolute_path.generic_string();
		}

		[[nodiscard]] std::string MockHeaderName(const ClassModel& class_model)
		{
			if (!class_model.mock_header_name.empty())
			{
				return class_model.mock_header_name;
			}
			return DefaultMockHeaderName(class_model.name);
		}

		[[nodiscard]] std::string FakeSourceName(const ClassModel& class_model)
		{
			if (!class_model.fake_source_name.empty())
			{
				return class_model.fake_source_name;
			}
			return DefaultFakeSourceName(class_model.name);
		}

		void CountFileName(std::map<std::string, std::size_t>& counts, std::string name)
		{
			const auto [iterator, inserted] = counts.emplace(std::move(name), 0U);
			(void)inserted;
			++iterator->second;
		}

		[[nodiscard]] std::size_t FileNameCount(const std::map<std::string, std::size_t>& counts,
												const std::string& name)
		{
			const auto iterator = counts.find(name);
			if (iterator == counts.end())
			{
				return 0U;
			}
			return iterator->second;
		}

		[[nodiscard]] bool IsAsciiFileNameCharacter(const unsigned char character) noexcept
		{
			return (character >= static_cast<unsigned char>('a') &&
					character <= static_cast<unsigned char>('z')) ||
				(character >= static_cast<unsigned char>('A') &&
				 character <= static_cast<unsigned char>('Z')) ||
				(character >= static_cast<unsigned char>('0') &&
				 character <= static_cast<unsigned char>('9')) ||
				character == static_cast<unsigned char>('_');
		}

		[[nodiscard]] std::string SanitizeFileNameComponent(std::string_view component)
		{
			std::string text;
			text.reserve(component.size());
			for (const char raw_character : component)
			{
				const auto character = static_cast<unsigned char>(raw_character);
				if (IsAsciiFileNameCharacter(character))
				{
					text += static_cast<char>(character);
				}
				else
				{
					text += '_';
				}
			}

			if (text.empty())
			{
				return "unnamed";
			}
			return text;
		}

		[[nodiscard]] std::vector<std::string>
		QualifiedFileNameComponents(const ClassModel& class_model)
		{
			std::vector<std::string> components = class_model.namespaces;
			if (components.empty() && !class_model.qualified_name.empty())
			{
				std::string_view qualified_name = class_model.qualified_name;
				while (!qualified_name.empty())
				{
					const auto separator = qualified_name.find("::");
					if (separator == std::string_view::npos)
					{
						components.emplace_back(qualified_name);
						break;
					}

					components.emplace_back(qualified_name.substr(0U, separator));
					qualified_name.remove_prefix(separator + 2U);
				}
			}

			if (components.empty() || components.back() != class_model.name)
			{
				components.push_back(class_model.name);
			}
			return components;
		}

		[[nodiscard]] std::string QualifiedFileNameToken(const ClassModel& class_model)
		{
			const auto components = QualifiedFileNameComponents(class_model);
			std::string text;
			for (const auto& component : components)
			{
				if (!text.empty())
				{
					text += '_';
				}
				text += SanitizeFileNameComponent(component);
			}
			return text;
		}

		[[nodiscard]] std::string QualifiedMockHeaderName(const ClassModel& class_model)
		{
			return "Mock_" + QualifiedFileNameToken(class_model) + ".h";
		}

		[[nodiscard]] std::string QualifiedFakeSourceName(const ClassModel& class_model)
		{
			return "Fake_" + QualifiedFileNameToken(class_model) + ".cpp";
		}

		[[nodiscard]] std::vector<ClassModel>
		ResolveQualifiedFilenameCollisions(std::span<const ClassModel> class_models)
		{
			std::map<std::string, std::size_t> mock_header_counts;
			std::map<std::string, std::size_t> fake_source_counts;
			for (const auto& class_model : class_models)
			{
				CountFileName(mock_header_counts, MockHeaderName(class_model));
				CountFileName(fake_source_counts, FakeSourceName(class_model));
			}

			std::vector<ClassModel> resolved;
			resolved.reserve(class_models.size());
			for (const auto& class_model : class_models)
			{
				auto resolved_class = class_model;
				const auto mock_collision =
					FileNameCount(mock_header_counts, MockHeaderName(class_model)) > 1U;
				const auto fake_collision =
					FileNameCount(fake_source_counts, FakeSourceName(class_model)) > 1U;
				if (mock_collision || fake_collision)
				{
					resolved_class.mock_header_name = QualifiedMockHeaderName(class_model);
					resolved_class.fake_source_name = QualifiedFakeSourceName(class_model);
				}
				resolved.push_back(std::move(resolved_class));
			}
			return resolved;
		}

		struct ClassReportEntry
		{
			std::string qualified_name;
			std::string mock_header;
			std::string fake_source;
			std::string default_mock_header;
			std::string default_fake_source;
			std::string source_header;
			std::size_t generated_methods = 0U;
			std::size_t unsupported_items = 0U;
			bool link_ready = true;
			std::vector<std::string> link_readiness_reasons;
			bool filename_collision = false;
		};

		struct UnsupportedReportEntry
		{
			std::string header;
			std::string class_name;
			std::string member;
			std::string reason;
			std::string suggested_action;
		};

		[[nodiscard]] ClassReportEntry MakeClassReportEntry(const ClassModel& class_model)
		{
			const auto mock_header = MockHeaderName(class_model);
			const auto fake_source = FakeSourceName(class_model);
			const auto default_mock_header = DefaultMockHeaderName(class_model.name);
			const auto default_fake_source = DefaultFakeSourceName(class_model.name);
			return ClassReportEntry{
				.qualified_name = QualifiedClassName(class_model),
				.mock_header = mock_header,
				.fake_source = fake_source,
				.default_mock_header = default_mock_header,
				.default_fake_source = default_fake_source,
				.source_header = SourceHeaderName(class_model),
				.generated_methods = class_model.mock_methods.size(),
				.unsupported_items = class_model.unsupported_items.size(),
				.link_ready = IsLinkReady(class_model),
				.link_readiness_reasons = LinkReadinessReasons(class_model),
				.filename_collision =
					mock_header != default_mock_header || fake_source != default_fake_source,
			};
		}

		[[nodiscard]] std::vector<ClassReportEntry>
		SortedClassReportEntries(std::span<const ClassModel> class_models)
		{
			std::vector<ClassReportEntry> entries;
			entries.reserve(class_models.size());
			for (const auto& class_model : class_models)
			{
				entries.push_back(MakeClassReportEntry(class_model));
			}

			std::sort(entries.begin(),
					  entries.end(),
					  [](const auto& lhs, const auto& rhs)
					  {
						  if (lhs.qualified_name != rhs.qualified_name)
						  {
							  return lhs.qualified_name < rhs.qualified_name;
						  }
						  return lhs.source_header < rhs.source_header;
					  });
			return entries;
		}

		[[nodiscard]] std::vector<UnsupportedReportEntry>
		SortedUnsupportedReportEntries(std::span<const ClassModel> class_models)
		{
			std::vector<UnsupportedReportEntry> entries;
			for (const auto& class_model : class_models)
			{
				const auto header = SourceHeaderName(class_model);
				const auto class_name = QualifiedClassName(class_model);
				for (const auto& unsupported : class_model.unsupported_items)
				{
					entries.push_back(UnsupportedReportEntry{
						.header = header,
						.class_name = class_name,
						.member = unsupported.member_signature.empty()
							? unsupported.name
							: unsupported.member_signature,
						.reason = unsupported.reason,
						.suggested_action = unsupported.suggested_action,
					});
				}
			}

			std::sort(entries.begin(),
					  entries.end(),
					  [](const auto& lhs, const auto& rhs)
					  {
						  if (lhs.header != rhs.header)
						  {
							  return lhs.header < rhs.header;
						  }
						  if (lhs.class_name != rhs.class_name)
						  {
							  return lhs.class_name < rhs.class_name;
						  }
						  if (lhs.member != rhs.member)
						  {
							  return lhs.member < rhs.member;
						  }
						  return lhs.reason < rhs.reason;
					  });
			return entries;
		}

		[[nodiscard]] std::size_t
		TotalGeneratedMethods(const std::vector<ClassReportEntry>& entries)
		{
			std::size_t count = 0U;
			for (const auto& entry : entries)
			{
				count += entry.generated_methods;
			}
			return count;
		}

		[[nodiscard]] std::size_t
		TotalUnsupportedItems(const std::vector<ClassReportEntry>& entries)
		{
			std::size_t count = 0U;
			for (const auto& entry : entries)
			{
				count += entry.unsupported_items;
			}
			return count;
		}

		[[nodiscard]] std::string GMockParameterType(const SimpleParameterModel& parameter)
		{
			if (!parameter.gmock_type.empty())
			{
				return parameter.gmock_type;
			}
			return parameter.type;
		}

		[[nodiscard]] std::string GMockReturnType(const SimpleMethodModel& method)
		{
			if (!method.gmock_return_type.empty())
			{
				return method.gmock_return_type;
			}
			return method.return_type;
		}

		[[nodiscard]] std::string
		JoinGMockParameterTypes(const std::vector<SimpleParameterModel>& parameters)
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
				text += GMockParameterType(parameters[index]);
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
				<< "#include <gmock/gmock.h>\n\n";

			std::vector<std::string> local_includes = {
				class_model.header_include,
				"MockFakeRuntime.h",
			};
			std::sort(local_includes.begin(), local_includes.end());
			for (const auto& include : local_includes)
			{
				out << "#include \"" << include << "\"\n";
			}
			out << '\n';

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
				out << member_indent << "MOCK_METHOD(" << GMockReturnType(method) << ", "
					<< method.name << ", (" << JoinGMockParameterTypes(method.parameters) << "), ("
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

			std::vector<std::string> local_includes = {
				class_model.header_include,
				MockHeaderFileName(class_model),
			};
			std::sort(local_includes.begin(), local_includes.end());
			for (const auto& include : local_includes)
			{
				out << "#include \"" << include << "\"\n";
			}
			out << '\n';

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
				.link_ready = class_model.link_ready && class_model.link_readiness_reasons.empty(),
			};
		}

		[[nodiscard]] GeneratedFile GenerateMockHeader(const SimpleClassModel& class_model)
		{
			return MakeGeneratedFile(MockHeaderFileName(class_model),
									 BuildMockHeaderContent(class_model),
									 GeneratedFileKind::MockHeader,
									 SourceClass(class_model));
		}

		[[nodiscard]] GeneratedFile GenerateFakeSource(const SimpleClassModel& class_model)
		{
			return MakeGeneratedFile(FakeSourceFileName(class_model),
									 BuildFakeSourceContent(class_model),
									 GeneratedFileKind::FakeSource,
									 SourceClass(class_model));
		}

		[[nodiscard]] SimpleClassModel ToSimpleClassModel(const ClassModel& class_model)
		{
			SimpleClassModel simple_class{
				.name = class_model.name,
				.namespaces = class_model.namespaces,
				.header_include = class_model.source_header.include_spelling,
				.mock_header_name = MockHeaderName(class_model),
				.fake_source_name = FakeSourceName(class_model),
				.methods = {},
				.link_ready = IsLinkReady(class_model),
				.link_readiness_reasons = LinkReadinessReasons(class_model),
			};
			simple_class.methods.reserve(class_model.mock_methods.size());

			for (const auto& method : class_model.mock_methods)
			{
				SimpleMethodModel simple_method{
					.return_type = method.return_type_spelling,
					.gmock_return_type = method.gmock_return_type_spelling,
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
						.gmock_type = parameter.gmock_type_spelling,
						.name = parameter.generated_name,
					});
				}

				simple_class.methods.push_back(std::move(simple_method));
			}

			return simple_class;
		}
	} // namespace

	std::vector<GeneratedFile> GenerateMinimalMockFake(const SimpleClassModel& class_model)
	{
		std::vector<GeneratedFile> files;
		files.push_back(GenerateMockHeader(class_model));
		files.push_back(GenerateFakeSource(class_model));
		files.push_back(MakeThreadLocalRuntimeHeader());
		SortGeneratedFiles(files);
		return files;
	}

	std::vector<GeneratedFile> GenerateMinimalMockFake(const ClassModel& class_model)
	{
		return GenerateMinimalMockFake(ToSimpleClassModel(class_model));
	}

	GeneratedFile GenerateAllMocksHeader(std::span<const GeneratedFile> files)
	{
		std::vector<std::filesystem::path> mock_headers;
		for (const auto& file : files)
		{
			if (file.kind == GeneratedFileKind::MockHeader)
			{
				mock_headers.push_back(file.relative_path);
			}
		}

		std::sort(mock_headers.begin(),
				  mock_headers.end(),
				  [](const auto& lhs, const auto& rhs)
				  {
					  return lhs.generic_string() < rhs.generic_string();
				  });
		mock_headers.erase(std::unique(mock_headers.begin(), mock_headers.end()),
						   mock_headers.end());

		std::ostringstream out;
		out << "#pragma once\n";
		if (!mock_headers.empty())
		{
			out << '\n';
		}
		for (const auto& mock_header : mock_headers)
		{
			out << "#include \"" << mock_header.generic_string() << "\"\n";
		}

		return MakeGeneratedFile(
			"AllMocks.h", out.str(), GeneratedFileKind::AllMocksHeader, std::nullopt);
	}

	GeneratedFile GenerateCMakeFragment(std::span<const GeneratedFile> files)
	{
		std::vector<std::filesystem::path> fake_sources;
		for (const auto& file : files)
		{
			if (file.kind == GeneratedFileKind::FakeSource &&
				(!file.source_class.has_value() || file.source_class->link_ready))
			{
				fake_sources.push_back(file.relative_path);
			}
		}

		std::sort(fake_sources.begin(),
				  fake_sources.end(),
				  [](const auto& lhs, const auto& rhs)
				  {
					  return lhs.generic_string() < rhs.generic_string();
				  });
		fake_sources.erase(std::unique(fake_sources.begin(), fake_sources.end()),
						   fake_sources.end());

		std::ostringstream out;
		out << "# Link generated FakeXXX.cpp files instead of the corresponding product .cpp "
			   "files.\n"
			<< "# Do not link both implementations in the same target.\n\n"
			<< "set(MOCKFAKE_GENERATED_SOURCES\n";
		for (const auto& fake_source : fake_sources)
		{
			out << "\t\"${CMAKE_CURRENT_LIST_DIR}/" << fake_source.generic_string() << "\"\n";
		}
		out << ")\n\n"
			<< "set(MOCKFAKE_GENERATED_INCLUDE_DIR\n"
			<< "\t\"${CMAKE_CURRENT_LIST_DIR}\"\n"
			<< ")\n";

		return MakeGeneratedFile(
			"CMakeLists.fragment.cmake", out.str(), GeneratedFileKind::CMakeFragment, std::nullopt);
	}

	GeneratedFile GenerateManifestJson(std::span<const ClassModel> class_models)
	{
		const auto entries = SortedClassReportEntries(class_models);
		const auto generated_methods = TotalGeneratedMethods(entries);
		const auto unsupported_items = TotalUnsupportedItems(entries);

		std::ostringstream out;
		out << "{\n"
			<< "  \"summary\": {\n"
			<< "    \"classes\": " << entries.size() << ",\n"
			<< "    \"link_ready_classes\": "
			<< std::count_if(entries.begin(),
							 entries.end(),
							 [](const auto& entry)
							 {
								 return entry.link_ready;
							 })
			<< ",\n"
			<< "    \"not_link_ready_classes\": "
			<< std::count_if(entries.begin(),
							 entries.end(),
							 [](const auto& entry)
							 {
								 return !entry.link_ready;
							 })
			<< ",\n"
			<< "    \"generated_methods\": " << generated_methods << ",\n"
			<< "    \"unsupported_items\": " << unsupported_items << ",\n"
			<< "    \"diagnostic_summary\": {\n"
			<< "      \"warnings\": " << unsupported_items << ",\n"
			<< "      \"errors\": 0\n"
			<< "    }\n"
			<< "  },\n"
			<< "  \"classes\": [\n";

		for (std::size_t index = 0U; index < entries.size(); ++index)
		{
			const auto& entry = entries[index];
			out << "    {\n"
				<< "      \"qualified_name\": " << JsonString(entry.qualified_name) << ",\n"
				<< "      \"mock_header\": " << JsonString(entry.mock_header) << ",\n"
				<< "      \"fake_source\": " << JsonString(entry.fake_source) << ",\n";
			if (entry.filename_collision)
			{
				out << "      \"filename_collision\": {\n"
					<< "        \"policy\": \"qualified-filename\",\n"
					<< "        \"default_mock_header\": " << JsonString(entry.default_mock_header)
					<< ",\n"
					<< "        \"resolved_mock_header\": " << JsonString(entry.mock_header)
					<< ",\n"
					<< "        \"default_fake_source\": " << JsonString(entry.default_fake_source)
					<< ",\n"
					<< "        \"resolved_fake_source\": " << JsonString(entry.fake_source) << "\n"
					<< "      },\n";
			}
			out << "      \"source_header\": " << JsonString(entry.source_header) << ",\n"
				<< "      \"generated_methods\": " << entry.generated_methods << ",\n"
				<< "      \"unsupported_methods\": " << entry.unsupported_items << ",\n"
				<< "      \"unsupported_items\": " << entry.unsupported_items << ",\n"
				<< "      \"link_ready\": " << (entry.link_ready ? "true" : "false") << ",\n"
				<< "      \"link_readiness_reasons\": ";
			WriteJsonStringArray(out, entry.link_readiness_reasons, "      ");
			out << ",\n"
				<< "      \"diagnostic_summary\": {\n"
				<< "        \"warnings\": " << entry.unsupported_items << ",\n"
				<< "        \"errors\": 0\n"
				<< "      }\n"
				<< "    }";
			if (index + 1U != entries.size())
			{
				out << ',';
			}
			out << '\n';
		}

		out << "  ]\n"
			<< "}\n";

		return MakeGeneratedFile(
			"manifest.json", out.str(), GeneratedFileKind::Manifest, std::nullopt);
	}

	GeneratedFile GenerateGenerationReport(std::span<const ClassModel> class_models)
	{
		const auto class_entries = SortedClassReportEntries(class_models);
		const auto unsupported_entries = SortedUnsupportedReportEntries(class_models);
		const auto generated_methods = TotalGeneratedMethods(class_entries);
		const auto unsupported_items = TotalUnsupportedItems(class_entries);

		std::ostringstream out;
		out << "# mockfakegen generation report\n\n"
			<< "## Summary\n\n"
			<< "| Classes | Link-ready classes | Not link-ready classes | Generated methods | "
			   "Unsupported items | Warnings | Errors |\n"
			<< "|---:|---:|---:|---:|---:|---:|---:|\n"
			<< "| " << class_entries.size() << " | "
			<< std::count_if(class_entries.begin(),
							 class_entries.end(),
							 [](const auto& entry)
							 {
								 return entry.link_ready;
							 })
			<< " | "
			<< std::count_if(class_entries.begin(),
							 class_entries.end(),
							 [](const auto& entry)
							 {
								 return !entry.link_ready;
							 })
			<< " | " << generated_methods << " | " << unsupported_items << " | "
			<< unsupported_items << " | 0 |\n\n"
			<< "## Link Replacement Notice\n\n"
			<< "Do not link generated `FakeXXX.cpp` files together with the corresponding "
			   "product `.cpp` files in the same test target. Link each generated fake "
			   "source instead of the product implementation it replaces.\n\n"
			<< "## Generated Classes\n\n"
			<< "| Class | Source header | Mock header | Fake source | Link ready | Link-readiness "
			   "reason | Generated methods | Unsupported items |\n"
			<< "|---|---|---|---|---|---|---:|---:|\n";

		for (const auto& entry : class_entries)
		{
			std::string link_readiness_reason;
			for (std::size_t reason_index = 0U; reason_index < entry.link_readiness_reasons.size();
				 ++reason_index)
			{
				if (reason_index != 0U)
				{
					link_readiness_reason += "; ";
				}
				link_readiness_reason += entry.link_readiness_reasons[reason_index];
			}

			out << "| " << MarkdownCell(entry.qualified_name) << " | "
				<< MarkdownCell(entry.source_header) << " | " << MarkdownCell(entry.mock_header)
				<< " | " << MarkdownCell(entry.fake_source) << " | "
				<< (entry.link_ready ? "yes" : "no") << " | " << MarkdownCell(link_readiness_reason)
				<< " | " << entry.generated_methods << " | " << entry.unsupported_items << " |\n";
		}

		out << "\n## Unsupported Items\n\n";
		if (unsupported_entries.empty())
		{
			out << "No unsupported items.\n";
		}
		else
		{
			out << "| Header | Class | Member | Reason | Suggested action |\n"
				<< "|---|---|---|---|---|\n";
			for (const auto& entry : unsupported_entries)
			{
				out << "| " << MarkdownCell(entry.header) << " | " << MarkdownCell(entry.class_name)
					<< " | " << MarkdownCell(entry.member) << " | " << MarkdownCell(entry.reason)
					<< " | " << MarkdownCell(entry.suggested_action) << " |\n";
			}
		}

		return MakeGeneratedFile(
			"generation_report.md", out.str(), GeneratedFileKind::Report, std::nullopt);
	}

	std::vector<GeneratedFile> GenerateMockFakeProject(std::span<const ClassModel> class_models,
													   ProjectGenerationOptions options)
	{
		const auto resolved_class_models = ResolveQualifiedFilenameCollisions(class_models);
		std::vector<GeneratedFile> files;
		files.reserve((resolved_class_models.size() * 2U) + 4U);
		for (const auto& class_model : resolved_class_models)
		{
			const auto simple_class = ToSimpleClassModel(class_model);
			files.push_back(GenerateMockHeader(simple_class));
			files.push_back(GenerateFakeSource(simple_class));
		}

		files.push_back(MakeRuntimeHeader(options.registry_mode));
		if (options.emit_all_mocks)
		{
			files.push_back(GenerateAllMocksHeader(files));
		}
		if (options.emit_cmake_fragment)
		{
			files.push_back(GenerateCMakeFragment(files));
		}
		if (options.emit_manifest)
		{
			files.push_back(GenerateManifestJson(resolved_class_models));
		}
		if (options.emit_report)
		{
			files.push_back(GenerateGenerationReport(resolved_class_models));
		}

		SortGeneratedFiles(files);
		return files;
	}
} // namespace mockfakegen
