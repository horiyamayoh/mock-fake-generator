#include "model/ProjectModel.h"

#include <algorithm>
#include <string>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::string HeaderSortKey(const HeaderModel& header)
		{
			return header.project_relative_path.generic_string();
		}

		[[nodiscard]] std::string UnsupportedSortKey(const UnsupportedItem& item)
		{
			return item.kind + '\n' + item.name + '\n' + item.reason;
		}

		[[nodiscard]] bool SourceLocationLess(const SourceLocation& lhs, const SourceLocation& rhs)
		{
			if (lhs.file != rhs.file)
			{
				return lhs.file.generic_string() < rhs.file.generic_string();
			}
			if (lhs.line != rhs.line)
			{
				return lhs.line < rhs.line;
			}
			return lhs.column < rhs.column;
		}

		[[nodiscard]] bool UnsupportedLess(const UnsupportedItem& lhs, const UnsupportedItem& rhs)
		{
			if (SourceLocationLess(lhs.source_range.begin, rhs.source_range.begin))
			{
				return true;
			}
			if (SourceLocationLess(rhs.source_range.begin, lhs.source_range.begin))
			{
				return false;
			}
			return UnsupportedSortKey(lhs) < UnsupportedSortKey(rhs);
		}

		void SortClassDiagnostics(ClassModel& class_model)
		{
			std::stable_sort(class_model.unsupported_items.begin(),
							 class_model.unsupported_items.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return UnsupportedLess(lhs, rhs);
							 });
		}
	} // namespace

	std::string BuildQualifiedName(const std::vector<std::string>& namespaces,
								   const std::string& name)
	{
		std::string qualified_name;
		for (const auto& namespace_part : namespaces)
		{
			if (!qualified_name.empty())
			{
				qualified_name += "::";
			}
			qualified_name += namespace_part;
		}

		if (!qualified_name.empty())
		{
			qualified_name += "::";
		}
		qualified_name += name;
		return qualified_name;
	}

	std::string DefaultMockName(const std::string& class_name)
	{
		return "Mock" + class_name;
	}

	std::string DefaultMockHeaderName(const std::string& class_name)
	{
		return DefaultMockName(class_name) + ".h";
	}

	std::string DefaultFakeSourceName(const std::string& class_name)
	{
		return "Fake" + class_name + ".cpp";
	}

	void SortProjectModel(ProjectModel& project)
	{
		std::stable_sort(project.headers.begin(),
						 project.headers.end(),
						 [](const auto& lhs, const auto& rhs)
						 {
							 return HeaderSortKey(lhs) < HeaderSortKey(rhs);
						 });
		std::stable_sort(project.classes.begin(),
						 project.classes.end(),
						 [](const auto& lhs, const auto& rhs)
						 {
							 return lhs.qualified_name < rhs.qualified_name;
						 });

		for (auto& class_model : project.classes)
		{
			SortClassDiagnostics(class_model);
		}
		std::stable_sort(project.unsupported_items.begin(),
						 project.unsupported_items.end(),
						 [](const auto& lhs, const auto& rhs)
						 {
							 return UnsupportedLess(lhs, rhs);
						 });
	}
} // namespace mockfakegen
