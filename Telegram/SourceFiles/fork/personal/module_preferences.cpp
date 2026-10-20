#include "fork/personal/module_preferences.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <utility>

namespace Fork::Personal {
namespace {

class PrivateTemporaryFile final {
public:
	explicit PrivateTemporaryFile(const std::filesystem::path &path);
	PrivateTemporaryFile(const PrivateTemporaryFile&) = delete;
	PrivateTemporaryFile &operator=(const PrivateTemporaryFile&) = delete;
	~PrivateTemporaryFile();
	[[nodiscard]] int descriptor() const;
	[[nodiscard]] bool commit(const std::filesystem::path &path);

private:
	std::string _path;
	int _descriptor = -1;

};

PrivateTemporaryFile::PrivateTemporaryFile(const std::filesystem::path &path)
: _path(path.string() + ".tmp.XXXXXX")
, _descriptor(::mkstemp(_path.data())) {
	if (_descriptor < 0) {
		_path.clear();
	}
}

PrivateTemporaryFile::~PrivateTemporaryFile() {
	if (_descriptor >= 0) {
		::close(_descriptor);
	}
	if (!_path.empty()) {
		::unlink(_path.c_str());
	}
}

int PrivateTemporaryFile::descriptor() const {
	return _descriptor;
}

bool PrivateTemporaryFile::commit(const std::filesystem::path &path) {
	if (_descriptor < 0 || ::fsync(_descriptor) != 0) {
		return false;
	}
	if (::close(std::exchange(_descriptor, -1)) != 0) {
		return false;
	}
	if (::rename(_path.c_str(), path.c_str()) != 0) {
		return false;
	}
	_path.clear();
	return true;
}

} // namespace

ModulePreferences ReadPreferences(const std::filesystem::path &path) {
	if (path.empty()) {
		return {};
	}
	auto error = std::error_code();
	const auto exists = std::filesystem::exists(path, error);
	if (error || (exists && std::filesystem::file_size(path, error) > 65536) || error) {
		return { {}, false };
	}
	if (!exists) {
		return {};
	}
	auto file = std::ifstream(path);
	auto header = std::string();
	if (!std::getline(file, header) || header != "SeeGramPersonalModules 1") {
		return { {}, false };
	}
	auto result = ModulePreferences();
	for (auto id = std::string(); std::getline(file, id);) {
		if (id.empty() || id.size() > 64 || result.enabled.size() >= 128
			|| id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-")
				!= std::string::npos) {
			return { {}, false };
		}
		result.enabled.push_back(id);
	}
	result.valid = file.eof();
	return result;
}

bool WritePrivateFile(const std::filesystem::path &path, std::string_view content) {
	if (path.empty()) {
		return true;
	}
	auto error = std::error_code();
	std::filesystem::create_directories(path.parent_path(), error);
	if (error) {
		return false;
	}
	auto temporary = PrivateTemporaryFile(path);
	const auto descriptor = temporary.descriptor();
	if (descriptor < 0) {
		return false;
	}
	auto written = std::size_t(0);
	while (written < content.size()) {
		const auto count = ::write(descriptor, content.data() + written,
			content.size() - written);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			break;
		}
		written += count;
	}
	return written == content.size() && temporary.commit(path);
}

bool WritePreferences(
		const std::filesystem::path &path,
		const std::vector<std::string> &enabled) {
	auto content = std::string("SeeGramPersonalModules 1\n");
	for (const auto &id : enabled) {
		content += id + '\n';
	}
	return WritePrivateFile(path, content);
}

} // namespace Fork::Personal
