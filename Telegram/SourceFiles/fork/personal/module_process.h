#pragma once

#include "fork/personal/module_manager.h"

namespace Fork::Personal {

class ModuleProcess final {
public:
	ModuleProcess() = default;
	ModuleProcess(const ModuleProcess&) = delete;
	ModuleProcess &operator=(const ModuleProcess&) = delete;
	~ModuleProcess();
	[[nodiscard]] bool start(const ModuleDefinition &definition);
	[[nodiscard]] bool send(std::string_view message);
	[[nodiscard]] bool alive();
	[[nodiscard]] std::vector<std::string> readLines();
	[[nodiscard]] bool invalid() const;

private:
	int _pid = -1;
	int _socket = -1;
	bool _invalid = false;
	std::string _buffer;

};

} // namespace Fork::Personal
