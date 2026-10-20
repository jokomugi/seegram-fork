#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fork::Personal {

inline constexpr auto kModuleInterface = 1;

enum class ModuleState { Disabled, Checking, Running, Failed, Incompatible };
enum class ModuleError {
	None, InterfaceMismatch, StartFailed, CheckFailed, Timeout,
	ProcessExited, InvalidResponse, PreferencesUnavailable,
};

struct ModuleDefinition {
	std::string id;
	std::string version;
	std::filesystem::path executable;
	std::vector<std::string> arguments;
	int interfaceVersion = kModuleInterface;
	bool (*checkHost)() = nullptr;
};

struct ModuleSnapshot {
	std::string id;
	std::string version;
	ModuleState state = ModuleState::Disabled;
	ModuleError error = ModuleError::None;
	bool requested = false;
	bool operator==(const ModuleSnapshot&) const = default;
};

[[nodiscard]] std::string_view StateCode(ModuleState state);
[[nodiscard]] std::string_view ErrorCode(ModuleError error);

struct ManagerOptions {
	std::filesystem::path preferences;
	std::string applicationVersion;
	std::chrono::milliseconds checkTimeout{3000};
	std::chrono::milliseconds heartbeatInterval{5000};
};

class ModuleManager final {
public:
	ModuleManager(std::vector<ModuleDefinition> definitions, ManagerOptions options = {});
	~ModuleManager();
	void setEnabled(std::string_view id, bool enabled);
	void poll();
	[[nodiscard]] std::vector<ModuleSnapshot> modules() const;
	[[nodiscard]] std::string diagnosticReport() const;

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

};

} // namespace Fork::Personal
