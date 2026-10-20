#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Fork::Personal {

struct ModulePreferences {
	std::vector<std::string> enabled;
	bool valid = true;
};

[[nodiscard]] ModulePreferences ReadPreferences(const std::filesystem::path &path);
[[nodiscard]] bool WritePreferences(
	const std::filesystem::path &path,
	const std::vector<std::string> &enabled);
[[nodiscard]] bool WritePrivateFile(const std::filesystem::path &path, std::string_view content);

} // namespace Fork::Personal
