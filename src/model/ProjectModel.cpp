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

		[[nodiscard]] std::string MethodSortKey(const MethodModel& method)
		{
			if (!method.signature_for_report.empty())
			{
				return method.signature_for_report;
			}

			return method.name;
		}

		[[nodiscard]] std::string UnsupportedSortKey(const UnsupportedItem& item)
		{
			return item.kind + '\n' + item.name + '\n' + item.reason;
		}

		void SortClassModel(ClassModel& class_model)
		{
			std::stable_sort(class_model.mock_methods.begin(),
							 class_model.mock_methods.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return MethodSortKey(lhs) < MethodSortKey(rhs);
							 });
			std::stable_sort(class_model.fake_methods.begin(),
							 class_model.fake_methods.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return MethodSortKey(lhs) < MethodSortKey(rhs);
							 });
			std::stable_sort(class_model.fake_constructors.begin(),
							 class_model.fake_constructors.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return lhs.signature_for_report < rhs.signature_for_report;
							 });
			std::stable_sort(class_model.fake_destructors.begin(),
							 class_model.fake_destructors.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return lhs.signature_for_report < rhs.signature_for_report;
							 });
			std::stable_sort(class_model.static_data_members.begin(),
							 class_model.static_data_members.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return lhs.signature_for_report < rhs.signature_for_report;
							 });
			std::stable_sort(class_model.unsupported_items.begin(),
							 class_model.unsupported_items.end(),
							 [](const auto& lhs, const auto& rhs)
							 {
								 return UnsupportedSortKey(lhs) < UnsupportedSortKey(rhs);
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
			SortClassModel(class_model);
		}
		std::stable_sort(project.unsupported_items.begin(),
						 project.unsupported_items.end(),
						 [](const auto& lhs, const auto& rhs)
						 {
							 return UnsupportedSortKey(lhs) < UnsupportedSortKey(rhs);
						 });
	}
} // namespace mockfakegen
