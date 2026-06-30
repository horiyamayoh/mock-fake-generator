#include "generator/CodeGenerator.h"

#include <algorithm>
#include <filesystem>
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

		struct ClassReportEntry
		{
			std::string qualified_name;
			std::string mock_header;
			std::string fake_source;
			std::string source_header;
			std::size_t generated_methods = 0U;
			std::size_t unsupported_items = 0U;
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
			return ClassReportEntry{
				.qualified_name = class_model.qualified_name.empty()
					? BuildQualifiedName(class_model.namespaces, class_model.name)
					: class_model.qualified_name,
				.mock_header = MockHeaderName(class_model),
				.fake_source = FakeSourceName(class_model),
				.source_header = SourceHeaderName(class_model),
				.generated_methods = class_model.mock_methods.size(),
				.unsupported_items = class_model.unsupported_items.size(),
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
				const auto class_name = class_model.qualified_name.empty()
					? BuildQualifiedName(class_model.namespaces, class_model.name)
					: class_model.qualified_name;
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
				mock_class_name + ".h",
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
			};
		}

		[[nodiscard]] GeneratedFile GenerateMockHeader(const SimpleClassModel& class_model)
		{
			return MakeGeneratedFile("Mock" + class_model.name + ".h",
									 BuildMockHeaderContent(class_model),
									 GeneratedFileKind::MockHeader,
									 SourceClass(class_model));
		}

		[[nodiscard]] GeneratedFile GenerateFakeSource(const SimpleClassModel& class_model)
		{
			return MakeGeneratedFile("Fake" + class_model.name + ".cpp",
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
				.methods = {},
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
			if (file.kind == GeneratedFileKind::FakeSource)
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
				<< "      \"fake_source\": " << JsonString(entry.fake_source) << ",\n"
				<< "      \"source_header\": " << JsonString(entry.source_header) << ",\n"
				<< "      \"generated_methods\": " << entry.generated_methods << ",\n"
				<< "      \"unsupported_methods\": " << entry.unsupported_items << ",\n"
				<< "      \"unsupported_items\": " << entry.unsupported_items << ",\n"
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
			<< "| Classes | Generated methods | Unsupported items | Warnings | Errors |\n"
			<< "|---:|---:|---:|---:|---:|\n"
			<< "| " << class_entries.size() << " | " << generated_methods << " | "
			<< unsupported_items << " | " << unsupported_items << " | 0 |\n\n"
			<< "## Link Replacement Notice\n\n"
			<< "Do not link generated `FakeXXX.cpp` files together with the corresponding "
			   "product `.cpp` files in the same test target. Link each generated fake "
			   "source instead of the product implementation it replaces.\n\n"
			<< "## Generated Classes\n\n"
			<< "| Class | Source header | Mock header | Fake source | Generated methods | "
			   "Unsupported items |\n"
			<< "|---|---|---|---|---:|---:|\n";

		for (const auto& entry : class_entries)
		{
			out << "| " << MarkdownCell(entry.qualified_name) << " | "
				<< MarkdownCell(entry.source_header) << " | " << MarkdownCell(entry.mock_header)
				<< " | " << MarkdownCell(entry.fake_source) << " | " << entry.generated_methods
				<< " | " << entry.unsupported_items << " |\n";
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
		std::vector<GeneratedFile> files;
		files.reserve((class_models.size() * 2U) + 4U);
		for (const auto& class_model : class_models)
		{
			const auto simple_class = ToSimpleClassModel(class_model);
			files.push_back(GenerateMockHeader(simple_class));
			files.push_back(GenerateFakeSource(simple_class));
		}

		files.push_back(MakeThreadLocalRuntimeHeader());
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
			files.push_back(GenerateManifestJson(class_models));
		}
		if (options.emit_report)
		{
			files.push_back(GenerateGenerationReport(class_models));
		}

		SortGeneratedFiles(files);
		return files;
	}
} // namespace mockfakegen
