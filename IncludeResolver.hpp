// Copyright (c) Nicolas VENTER All rights reserved.

#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// small wrapper class for pretty display of path with slash instead of backslash
class PrettyPath : public std::filesystem::path
{
	using parent_type = std::filesystem::path;
	using parent_type::parent_type;

public:
	std::string prettyString() const;
	friend std::ostream& operator<<(std::ostream& os, const PrettyPath& prettyPath);
};

struct IncludeResolverSettings
{
	// list of folders to parse
	std::vector<std::filesystem::path> toParseFolderList;
	// set of folders to include in order to help resolve includes
	std::vector<std::filesystem::path> includeFolderList;
	// list of folders that can be used in order to resolve includes
	std::vector<std::filesystem::path> resolveFolderList;
};

struct IncludeLocation
{
	// file where include was unresolved
	PrettyPath filePath;
	// line of the include
	uint32_t line;

	friend bool operator<(const IncludeLocation& lhs, const IncludeLocation& rhs);

	// can be used to display include location
	friend std::ostream& operator<<(std::ostream& os, const IncludeLocation& unresolvedInclude);
};

// include that have not been resolved
struct UnresolvedInclude : public IncludeLocation
{
	// text describing the include
	std::string include;

	friend bool operator<(const UnresolvedInclude& lhs, const UnresolvedInclude& rhs);

	// can be used to display unresolved include
	friend std::ostream& operator<<(std::ostream& os, const UnresolvedInclude& unresolvedInclude);
};

// include that can be resolve in several ways
struct ConflictedInclude
{
	// set of locations where the include is done
	std::set<IncludeLocation> includeLocationSet;

	// set of folders that can all resolve the include
	std::set<PrettyPath> resolveIncludeFolderSet;

	// can be used to display conflicted include
	friend std::ostream& operator<<(std::ostream& os, const ConflictedInclude& conflictedInclude);
};

struct IncludeResolverResult
{
	// set of invalid paths from Settings
	std::set<std::filesystem::path> invalidPathSet;
	// set of unresolved includes
	std::set<UnresolvedInclude> unresolvedIncludeSet;
	// map of conflicted includes
	// key: text describing the include, value: reference of the include
	std::multimap<std::filesystem::path, ConflictedInclude> conflictedIncludeMap;
	// set of folders to include in order to resolve all includes
	std::set<PrettyPath> resolveIncludeFolderSet;
};

namespace include_resolver
{
	using ParseStatusCallback = std::function<void(size_t current, size_t total, const PrettyPath& filePath)>;

	// compute all folders to include in order to resolve all includes
	static IncludeResolverResult computeIncludeResolve(
		const IncludeResolverSettings& settings,
		ParseStatusCallback parseStatusCallback = [](size_t, size_t, const PrettyPath&) {});

	namespace example
	{
		// example of parse status callback that displays the parse status
		static void displayParseStatus(size_t current, size_t total, const PrettyPath& filePath);
	} // namespace example
} // namespace include_resolver

namespace std
{
	template <> struct hash<std::filesystem::path>
	{
		std::size_t operator()(const std::filesystem::path& path) const;
	};
} // namespace std

// ============================================================
// ============================================================
// ===================== Implementations  =====================
// ============================================================
// ============================================================

std::string PrettyPath::prettyString() const
{
	std::string s = string();
	std::replace(s.begin(), s.end(), '\\', '/');
	return s;
}

std::ostream& operator<<(std::ostream& os, const PrettyPath& prettyPath) { return os << prettyPath.prettyString(); }

bool operator<(const IncludeLocation& lhs, const IncludeLocation& rhs)
{
	return lhs.filePath != rhs.filePath ? lhs.filePath < rhs.filePath : lhs.line < rhs.line;
}

// can be used to display include location
std::ostream& operator<<(std::ostream& os, const IncludeLocation& includeLocation)
{
	return os << includeLocation.filePath << ":" << includeLocation.line;
}

bool operator<(const UnresolvedInclude& lhs, const UnresolvedInclude& rhs)
{
	return static_cast<IncludeLocation>(lhs) < static_cast<IncludeLocation>(rhs);
}
std::ostream& operator<<(std::ostream& os, const UnresolvedInclude& unresolvedInclude)
{
	return os << static_cast<IncludeLocation>(unresolvedInclude) << " : " << unresolvedInclude.include;
}

std::ostream& operator<<(std::ostream& os, const ConflictedInclude& conflictedInclude)
{
	os << "\tincluded by:\n\t[";
	for (const auto& includeLocation : conflictedInclude.includeLocationSet) os << "\n\t\t" << includeLocation;
	os << "\n\t]\n\tcan be resolved by:\n\t[";
	for (const auto& resolveIncludeFolder : conflictedInclude.resolveIncludeFolderSet) os << "\n\t\t" << resolveIncludeFolder;
	return os << "\n\t]";
}

namespace include_resolver
{
	namespace utility
	{
		static bool bStartWith(const std::string_view& str, const std::string_view& prefix, std::string_view& subStr)
		{
			if (str.size() < prefix.size()) return false;
			bool result = str.substr(0, prefix.size()) == prefix;
			if (result) subStr = str.substr(prefix.size());
			return result;
		}

		static bool bEndWith(const std::string_view& str, const std::string_view& suffix, std::string_view& subStr)
		{
			if (str.size() < suffix.size()) return false;
			auto offset = str.size() - suffix.size();
			bool result = str.substr(offset) == suffix;
			if (result) subStr = str.substr(0, offset);
			return result;
		}

		static bool isCppFile(const std::string& filePath)
		{
			static auto cppExtensionList = {".h", ".hpp", ".hxx", ".hh", ".c", ".cpp", ".cxx"};
			static std::string_view subStr;
			for (auto& cppExtension : cppExtensionList)
				if (bEndWith(filePath, cppExtension, subStr)) return true;
			return false;
		}

		static void updateCppFileList(const std::filesystem::path& folderPath, std::vector<std::filesystem::path>& cppFileList)
		{
			for (const auto& file : std::filesystem::directory_iterator(folderPath))
			{
				if (file.is_directory())
				{
					updateCppFileList(file.path(), cppFileList);
				}
				else
				{
					if (isCppFile(file.path().string()))
						cppFileList.push_back(std::filesystem::canonical(std::filesystem::absolute(file.path())));
				}
			}
		}
	} // namespace utility

	static IncludeResolverResult computeIncludeResolve(
		const IncludeResolverSettings& settings, ParseStatusCallback parseStatusCallback)
	{
		using namespace utility;

		IncludeResolverResult result;

		std::vector<std::filesystem::path> cppFileToParseList;
		for (const auto& toParseFolder : settings.toParseFolderList) updateCppFileList(toParseFolder, cppFileToParseList);

		for (const auto& includeFolder : settings.includeFolderList)
		{
			if (std::filesystem::exists(includeFolder))
				result.resolveIncludeFolderSet.insert(
					PrettyPath(std::filesystem::canonical(std::filesystem::absolute(includeFolder))));
			else
				result.invalidPathSet.insert(includeFolder);
		}
		std::vector<std::filesystem::path> resolveFileList;
		for (const auto& resolveFolder : settings.resolveFolderList)
		{
			std::filesystem::path resolveFolderPath = resolveFolder;
			if (std::filesystem::exists(resolveFolderPath)) updateCppFileList(resolveFolder, resolveFileList);
			else
				result.invalidPathSet.insert(resolveFolder);
		}
		std::unordered_multimap<std::string, std::filesystem::path> resolveFileMultiMap;
		for (const auto& resolveFile : resolveFileList) resolveFileMultiMap.emplace(resolveFile.filename().string(), resolveFile);

		std::unordered_set<std::filesystem::path> cppFileToParseSet;
		for (const auto& cppFileToParse : cppFileToParseList) cppFileToParseSet.insert(cppFileToParse);

		for (size_t i = 0; i < cppFileToParseList.size(); ++i)
		{
			// copy done since cppFileToParseList can be reallocate at any time
			PrettyPath cppFileToParse = PrettyPath(cppFileToParseList[i]);
			const PrettyPath& filePath = cppFileToParse;

			parseStatusCallback((i + 1), cppFileToParseList.size(), cppFileToParse);

			std::ifstream ifs(static_cast<std::filesystem::path>(cppFileToParse));
			uint32_t lineIndex = 1;
			for (std::string line; std::getline(ifs, line); ++lineIndex)
			{
				std::string_view substr;
				if (!bStartWith(line, "#include ", substr)) continue;
				auto startPos = substr.find_first_of("\"<");
				if (startPos == std::string::npos) continue;
				auto endPos = substr.find_first_of("\">", startPos + 1);
				std::string include = static_cast<std::string>(substr.substr(startPos + 1, endPos - startPos - 1));

				const std::filesystem::path& includePath = cppFileToParse.parent_path() / std::filesystem::path(include);

				static auto addToParse = [&cppFileToParseSet, &cppFileToParseList](const std::filesystem::path& includePath)
				{
					std::filesystem::path canonicalPath = std::filesystem::canonical(std::filesystem::path(includePath));
					if (!cppFileToParseSet.count(canonicalPath))
					{
						cppFileToParseSet.insert(canonicalPath);
						cppFileToParseList.push_back(canonicalPath);
					}
				};

				if (std::filesystem::exists(includePath))
				{
					addToParse(includePath);
					continue;
				}

				static auto findProjectInclude = [&result](const std::string& include, std::filesystem::path& fileFound)
				{
					for (auto& resolveIncludeFolder : result.resolveIncludeFolderSet)
					{
						fileFound = resolveIncludeFolder / std::filesystem::path(include);
						if (std::filesystem::exists(fileFound)) return true;
					}
					return false;
				};

				static auto findInclude = [&resolveFileMultiMap](const std::string& include, ConflictedInclude& conflictedInclude)
				{
					std::string_view substr;
					std::string includeBaseName = std::filesystem::path(include).filename().string();
					auto [it, itEnd] = resolveFileMultiMap.equal_range(includeBaseName);
					for (; it != itEnd; ++it)
					{
						std::string s = it->second.string();
						std::replace(s.begin(), s.end(), '\\', '/');
						if (bEndWith(s, "/" + include, substr))
							conflictedInclude.resolveIncludeFolderSet.insert(PrettyPath(substr));
					}
					return !conflictedInclude.resolveIncludeFolderSet.empty();
				};

				ConflictedInclude conflictedInclude;
				auto it = result.conflictedIncludeMap.find(include);
				if (it != result.conflictedIncludeMap.end())
				{
					IncludeLocation includeLocation;
					includeLocation.filePath = filePath;
					includeLocation.line = lineIndex;
					it->second.includeLocationSet.insert(includeLocation);
					continue;
				}
				if (findInclude(include, conflictedInclude))
				{
					if (conflictedInclude.resolveIncludeFolderSet.size() == 1)
					{
						const PrettyPath& resolveIncludeFolderFound = *conflictedInclude.resolveIncludeFolderSet.begin();
						result.resolveIncludeFolderSet.insert(resolveIncludeFolderFound);
						addToParse(resolveIncludeFolderFound / std::filesystem::path(include));
					}
					else
					{
						IncludeLocation includeLocation;
						includeLocation.filePath = filePath;
						includeLocation.line = lineIndex;
						conflictedInclude.includeLocationSet.insert(includeLocation);
						result.conflictedIncludeMap.emplace(include, conflictedInclude);
						for (const auto& resolveIncludeFolder : conflictedInclude.resolveIncludeFolderSet)
						{
							addToParse(resolveIncludeFolder / std::filesystem::path(include));
						}
					}
					continue;
				}

				std::filesystem::path fileFound;
				if (findProjectInclude(include, fileFound))
				{
					addToParse(fileFound);
					continue;
				}

				UnresolvedInclude unresolvedInclude;
				unresolvedInclude.filePath = filePath;
				unresolvedInclude.include = include;
				unresolvedInclude.line = lineIndex;
				result.unresolvedIncludeSet.insert(unresolvedInclude);
			}
		}
		return result;
	}

	namespace example
	{
		static void displayParseStatus(size_t current, size_t total, const PrettyPath& filePath)
		{
			std::cout << "[" << current << "/" << total << "] " << filePath << std::endl;
		}
	} // namespace example
} // namespace include_resolver

namespace std
{
	std::size_t hash<std::filesystem::path>::operator()(const std::filesystem::path& path) const
	{
		static std::hash<std::string> stringHasher;
		return stringHasher(std::filesystem::absolute(path).string());
	}
} // namespace std
