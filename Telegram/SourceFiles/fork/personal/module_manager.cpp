#include "fork/personal/module_manager.h"
#include "fork/personal/module_process.h"
#include "fork/personal/module_preferences.h"

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Fork::Personal {
namespace {

void JsonString(std::ostream &stream, std::string_view value) {
	constexpr auto hex = "0123456789abcdef";
	stream << '"';
	for (const auto character : value) {
		const auto byte = static_cast<unsigned char>(character);
		if (byte == '"' || byte == '\\') {
			stream << '\\' << character;
		} else if (byte < 32) {
			stream << "\\u00" << hex[byte >> 4] << hex[byte & 15];
		} else {
			stream << character;
		}
	}
	stream << '"';
}

void WriteSnapshot(std::ostream &stream, const ModuleSnapshot &snapshot, std::int64_t timeMs = 0) {
	stream << "{\"id\":";
	JsonString(stream, snapshot.id);
	stream << ",\"version\":";
	JsonString(stream, snapshot.version);
	stream << ",\"state\":";
	JsonString(stream, StateCode(snapshot.state));
	stream << ",\"error\":";
	JsonString(stream, ErrorCode(snapshot.error));
	stream << ",\"requested\":" << (snapshot.requested ? "true" : "false");
	if (timeMs > 0) {
		stream << ",\"time_ms\":" << timeMs;
	}
	stream << '}';
}

} // namespace

std::string_view StateCode(ModuleState state) {
	constexpr auto names = std::array{
		"disabled", "checking", "running", "failed", "incompatible",
	};
	return names.at(static_cast<std::size_t>(state));
}

std::string_view ErrorCode(ModuleError error) {
	constexpr auto names = std::array{
		"none", "interface_mismatch", "start_failed", "check_failed", "timeout",
		"process_exited", "invalid_response", "preferences_unavailable",
	};
	return names.at(static_cast<std::size_t>(error));
}

struct ModuleManager::Impl {
	struct Entry {
		ModuleDefinition definition;
		ModuleSnapshot snapshot;
		std::unique_ptr<ModuleProcess> process;
		std::chrono::steady_clock::time_point deadline;
		bool awaitingHeartbeat = false;
	};
	struct Event {
		ModuleSnapshot snapshot;
		std::int64_t timeMs = 0;
	};
	std::vector<Entry> entries;
	ManagerOptions options;
	std::deque<Event> events;
	bool journalAvailable = true;

	std::filesystem::path journalPath() const {
		return options.preferences.empty() ? std::filesystem::path()
			: std::filesystem::path(options.preferences.string() + ".events");
	}

	void readJournal() {
		const auto path = journalPath();
		if (path.empty()) {
			return;
		}
		auto error = std::error_code();
		if (!std::filesystem::exists(path, error)) {
			journalAvailable = !error;
			return;
		}
		if (std::filesystem::file_size(path, error) > 65536 || error) {
			journalAvailable = false;
			return;
		}
		auto file = std::ifstream(path);
		auto line = std::string();
		journalAvailable = std::getline(file, line) && line == "SeeGramPersonalEvents 1";
		while (journalAvailable && std::getline(file, line)) {
			auto input = std::istringstream(line);
			auto event = Event();
			auto state = 0;
			auto code = 0;
			auto requested = 0;
			input >> event.timeMs >> state >> code >> requested
				>> std::quoted(event.snapshot.id) >> std::quoted(event.snapshot.version);
			const auto known = std::any_of(entries.begin(), entries.end(), [&](const Entry &entry) {
				return entry.definition.id == event.snapshot.id;
			});
			if (!input || !known || event.timeMs <= 0 || events.size() >= 64
				|| state < 0 || state > int(ModuleState::Incompatible)
				|| code < 0 || code > int(ModuleError::PreferencesUnavailable)
				|| requested < 0 || requested > 1 || event.snapshot.version.size() > 64
				|| event.snapshot.version.find_first_not_of(
					"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._-+")
						!= std::string::npos) {
				journalAvailable = false;
				break;
			}
			event.snapshot.state = ModuleState(state);
			event.snapshot.error = ModuleError(code);
			event.snapshot.requested = requested;
			events.push_back(std::move(event));
		}
		if (!journalAvailable) {
			events.clear();
		}
	}

	void record(const Entry &entry) {
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		events.push_back({ entry.snapshot, now });
		if (events.size() > 64) {
			events.pop_front();
		}
		auto content = std::ostringstream();
		content << "SeeGramPersonalEvents 1\n";
		for (const auto &event : events) {
			content << event.timeMs << ' ' << int(event.snapshot.state) << ' '
				<< int(event.snapshot.error) << ' ' << event.snapshot.requested << ' '
				<< std::quoted(event.snapshot.id) << ' '
				<< std::quoted(event.snapshot.version) << '\n';
		}
		journalAvailable = WritePrivateFile(journalPath(), content.str());
	}

	void start(Entry &entry) {
		entry.awaitingHeartbeat = false;
		entry.snapshot.error = ModuleError::None;
		entry.snapshot.state = ModuleState::Checking;
		if (entry.definition.interfaceVersion != kModuleInterface) {
			entry.snapshot.state = ModuleState::Incompatible;
			entry.snapshot.error = ModuleError::InterfaceMismatch;
			record(entry);
			return;
		}
		record(entry);
		entry.process = std::make_unique<ModuleProcess>();
		entry.deadline = std::chrono::steady_clock::now() + options.checkTimeout;
		if (!entry.process->start(entry.definition)
			|| !entry.process->send("CHECK 1\n")) {
			fail(entry, ModuleError::StartFailed);
		}
	}

	void fail(Entry &entry, ModuleError error) {
		entry.process.reset();
		entry.snapshot.state = ModuleState::Failed;
		entry.snapshot.error = error;
		record(entry);
	}
};

ModuleManager::ModuleManager(std::vector<ModuleDefinition> definitions, ManagerOptions options)
: _impl(std::make_unique<Impl>()) {
	_impl->options = std::move(options);
	for (const auto &definition : definitions) {
		_impl->entries.push_back({
			definition, { definition.id, definition.version }, nullptr, {}, false,
		});
	}
	_impl->readJournal();
	const auto preferences = ReadPreferences(_impl->options.preferences);
	for (auto &entry : _impl->entries) {
		if (!preferences.valid) {
			_impl->fail(entry, ModuleError::PreferencesUnavailable);
		} else if (std::find(preferences.enabled.begin(), preferences.enabled.end(),
			entry.definition.id) != preferences.enabled.end()) {
			entry.snapshot.requested = true;
			_impl->start(entry);
		}
	}
}

ModuleManager::~ModuleManager() = default;

void ModuleManager::setEnabled(std::string_view id, bool enabled) {
	for (auto &entry : _impl->entries) {
		if (entry.definition.id != id) {
			continue;
		}
		entry.process.reset();
		entry.snapshot.requested = enabled;
		entry.snapshot.error = ModuleError::None;
		entry.snapshot.state = enabled ? ModuleState::Checking : ModuleState::Disabled;
		auto requested = std::vector<std::string>();
		for (const auto &other : _impl->entries) {
			if (other.snapshot.requested) {
				requested.push_back(other.definition.id);
			}
		}
		if (!WritePreferences(_impl->options.preferences, requested)) {
			_impl->fail(entry, ModuleError::PreferencesUnavailable);
			return;
		}
		if (enabled) {
			_impl->start(entry);
		} else {
			_impl->record(entry);
		}
		return;
	}
}

void ModuleManager::poll() {
	for (auto &entry : _impl->entries) {
		if (!entry.process) {
			continue;
		}
		if (!entry.process->alive()) {
			_impl->fail(entry, ModuleError::ProcessExited);
			continue;
		}
		for (const auto &line : entry.process->readLines()) {
			if (entry.snapshot.state == ModuleState::Checking && line == "READY 1") {
				entry.snapshot.state = ModuleState::Running;
				_impl->record(entry);
				entry.deadline = std::chrono::steady_clock::now()
					+ _impl->options.heartbeatInterval;
			} else if (entry.snapshot.state == ModuleState::Running
				&& entry.awaitingHeartbeat && line == "PONG") {
				entry.awaitingHeartbeat = false;
				entry.deadline = std::chrono::steady_clock::now()
					+ _impl->options.heartbeatInterval;
			} else {
				_impl->fail(entry, ModuleError::InvalidResponse);
				break;
			}
		}
		if (entry.process && entry.process->invalid()) {
			_impl->fail(entry, ModuleError::InvalidResponse);
		}
		if (entry.process && std::chrono::steady_clock::now() >= entry.deadline) {
			if (entry.snapshot.state == ModuleState::Checking || entry.awaitingHeartbeat) {
				_impl->fail(entry, ModuleError::Timeout);
			} else if (!entry.process->send("PING\n")) {
				_impl->fail(entry, ModuleError::ProcessExited);
			} else {
				entry.awaitingHeartbeat = true;
				entry.deadline = std::chrono::steady_clock::now()
					+ _impl->options.checkTimeout;
			}
		}
	}
}

std::vector<ModuleSnapshot> ModuleManager::modules() const {
	auto result = std::vector<ModuleSnapshot>();
	for (const auto &entry : _impl->entries) {
		result.push_back(entry.snapshot);
	}
	return result;
}

std::string ModuleManager::diagnosticReport() const {
	auto stream = std::ostringstream();
	stream << "{\"schema\":1,\"application_version\":";
	JsonString(stream, _impl->options.applicationVersion);
	stream << ",\"host_interface\":" << kModuleInterface << ",\"modules\":[";
	auto first = true;
	for (const auto &entry : _impl->entries) {
		if (!first) {
			stream << ',';
		}
		first = false;
		WriteSnapshot(stream, entry.snapshot);
	}
	stream << "],\"events\":[";
	first = true;
	for (const auto &event : _impl->events) {
		if (!first) {
			stream << ',';
		}
		first = false;
		WriteSnapshot(stream, event.snapshot, event.timeMs);
	}
	stream << "],\"journal_available\":" << (_impl->journalAvailable ? "true" : "false") << "}\n";
	return stream.str();
}

} // namespace Fork::Personal
