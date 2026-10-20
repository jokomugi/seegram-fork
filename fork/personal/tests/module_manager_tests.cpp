#include "fork/personal/module_manager.h"

#include <iostream>
#include <fcntl.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace Fork::Personal;
using namespace std::chrono_literals;

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Predicate>
void Await(ModuleManager &manager, Predicate predicate) {
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (std::chrono::steady_clock::now() < deadline) {
		manager.poll();
		if (predicate(manager.modules())) {
			return;
		}
		std::this_thread::sleep_for(2ms);
	}
	throw std::runtime_error("Module states did not settle before the deadline");
}

int main(int argc, char *argv[]) {
	try {
		Require(argc >= 2, "Worker fixture path is required");
		if (argc > 2 && std::string(argv[2]) == "descriptor-isolation") {
			const auto descriptor = ::open("/dev/null", O_RDONLY);
			Require(descriptor >= 0, "Host descriptor must be open");
			auto manager = ModuleManager({
				{ "isolated", "1.0", argv[1], { "descriptor", std::to_string(descriptor) } },
			});
			manager.setEnabled("isolated", true);
			::close(descriptor);
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running;
			});
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "journal") {
			auto pattern = (std::filesystem::temp_directory_path()
				/ "seegram-personal-journal.XXXXXX").string();
			const auto directory = ::mkdtemp(pattern.data());
			Require(directory != nullptr, "Temporary profile must be created");
			auto options = ManagerOptions();
			options.preferences = std::filesystem::path(directory) / "modules.preferences";
			{
				auto manager = ModuleManager({
					{ "recoverable", "1.0", argv[1], { "secret" } },
				}, options);
				manager.setEnabled("recoverable", true);
				Await(manager, [](const auto &modules) {
					return modules[0].error == ModuleError::InvalidResponse;
				});
			}
			{
				auto manager = ModuleManager({
					{ "recoverable", "1.0", argv[1], { "healthy" } },
				}, options);
				Await(manager, [](const auto &modules) {
					return modules[0].state == ModuleState::Running;
				});
				std::cout << manager.diagnosticReport();
			}
			std::filesystem::remove_all(directory);
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "production") {
			auto manager = ModuleManager({
				{ "diagnostics", "1.0.0", argv[1], { "diagnostics" } },
			});
			manager.setEnabled("diagnostics", true);
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running;
			});
			manager.setEnabled("diagnostics", false);
			Require(manager.modules()[0].state == ModuleState::Disabled,
				"The production diagnostic module must stop on disable");
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "diagnostics") {
			auto options = ManagerOptions();
			options.applicationVersion = "7.3-personal.1\"test";
			auto manager = ModuleManager({
				{ "healthy", "1.0", argv[1], { "healthy" } },
				{ "untrusted-output", "1.0", argv[1], { "secret" } },
			}, options);
			manager.setEnabled("healthy", true);
			manager.setEnabled("untrusted-output", true);
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running
					&& modules[1].state == ModuleState::Failed;
			});
			std::cout << manager.diagnosticReport();
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "heartbeat") {
			auto options = ManagerOptions();
			options.heartbeatInterval = 50ms;
			options.checkTimeout = 250ms;
			auto manager = ModuleManager({
				{ "healthy", "1.0", argv[1], { "healthy" } },
				{ "stalled", "1.0", argv[1], { "hang-running" } },
			}, options);
			manager.setEnabled("healthy", true);
			manager.setEnabled("stalled", true);
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running
					&& modules[1].state == ModuleState::Running;
			});
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running
					&& modules[1].error == ModuleError::Timeout;
			});
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "restore") {
			auto pattern = (std::filesystem::temp_directory_path()
				/ "seegram-personal-test.XXXXXX").string();
			const auto directory = ::mkdtemp(pattern.data());
			Require(directory != nullptr, "Temporary profile must be created");
			auto options = ManagerOptions();
			options.preferences = std::filesystem::path(directory) / "modules.preferences";
			const auto definitions = std::vector<ModuleDefinition>{
				{ "healthy", "1.0", argv[1], { "healthy" } },
			};
			{
				auto manager = ModuleManager(definitions, options);
				manager.setEnabled("healthy", true);
				Await(manager, [](const auto &modules) {
					return modules[0].state == ModuleState::Running;
				});
			}
			{
				auto manager = ModuleManager(definitions, options);
				Await(manager, [](const auto &modules) {
					return modules[0].requested
						&& modules[0].state == ModuleState::Running;
				});
				manager.setEnabled("healthy", false);
				Require(manager.modules()[0].state == ModuleState::Disabled,
					"Disable must stop the module immediately");
			}
			{
				auto manager = ModuleManager(definitions, options);
				Require(!manager.modules()[0].requested,
					"Disabled preference must survive restart");
			}
			std::filesystem::remove_all(directory);
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "incompatible") {
			auto manager = ModuleManager({
				{ "newer", "2.0", argv[1], { "healthy" }, 2 },
			});
			manager.setEnabled("newer", true);
			Require(manager.modules()[0].state == ModuleState::Incompatible,
				"An incompatible module must be rejected before launch");
			Require(manager.modules()[0].error == ModuleError::InterfaceMismatch,
				"Compatibility rejection must explain the interface mismatch");
			return 0;
		}
		if (argc > 2 && std::string(argv[2]) == "timeout") {
			auto options = ManagerOptions();
			options.checkTimeout = 250ms;
			auto manager = ModuleManager({
				{ "healthy", "1.0", argv[1], { "healthy" } },
				{ "slow", "1.0", argv[1], { "hang" } },
			}, options);
			manager.setEnabled("healthy", true);
			manager.setEnabled("slow", true);
			Await(manager, [](const auto &modules) {
				return modules[0].state == ModuleState::Running
					&& modules[1].error == ModuleError::Timeout;
			});
			std::cout << "PASS: self-check timeout isolates a hung module\n";
			return 0;
		}
		auto manager = ModuleManager({
			{ "healthy", "1.0", argv[1], { "healthy" } },
			{ "broken", "1.0", argv[1], { "crash" } },
		});
		manager.setEnabled("healthy", true);
		manager.setEnabled("broken", true);
		Await(manager, [](const auto &modules) {
			return modules[0].state == ModuleState::Running
				&& modules[1].state == ModuleState::Failed;
		});
		Require(manager.modules()[1].error == ModuleError::ProcessExited,
			"The failed module must explain its process exit");
		std::cout << "PASS: crashing module leaves the other module running\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
