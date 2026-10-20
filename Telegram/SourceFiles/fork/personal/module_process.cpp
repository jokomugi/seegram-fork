#include "fork/personal/module_process.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace Fork::Personal {
namespace {

struct SpawnResources {
	SpawnResources() = default;
	SpawnResources(const SpawnResources&) = delete;
	SpawnResources &operator=(const SpawnResources&) = delete;
	~SpawnResources();

	std::array<int, 2> sockets{ -1, -1 };
	posix_spawn_file_actions_t actions{};
	posix_spawnattr_t attributes{};
	bool actionsReady = false;
	bool attributesReady = false;

};

SpawnResources::~SpawnResources() {
	if (actionsReady) {
		::posix_spawn_file_actions_destroy(&actions);
	}
	if (attributesReady) {
		::posix_spawnattr_destroy(&attributes);
	}
	for (const auto socket : sockets) {
		if (socket >= 0) {
			::close(socket);
		}
	}
}

} // namespace

ModuleProcess::~ModuleProcess() {
	if (_socket >= 0) {
		::close(_socket);
	}
	if (_pid > 0 && alive()) {
		::kill(_pid, SIGKILL);
		auto status = 0;
		while (::waitpid(_pid, &status, 0) < 0 && errno == EINTR) {
		}
	}
}

bool ModuleProcess::start(const ModuleDefinition &definition) {
	if (_socket >= 0 || !definition.executable.is_absolute()) {
		return false;
	}
	auto resources = SpawnResources();
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, resources.sockets.data()) != 0) {
		return false;
	}
	for (const auto socket : resources.sockets) {
		if (::fcntl(socket, F_SETFD, FD_CLOEXEC) < 0) {
			return false;
		}
	}
#ifdef SO_NOSIGPIPE
	const auto suppress = 1;
	if (::setsockopt(resources.sockets[0], SOL_SOCKET, SO_NOSIGPIPE,
		&suppress, sizeof(suppress)) != 0) {
		return false;
	}
#endif // SO_NOSIGPIPE
	resources.actionsReady = ::posix_spawn_file_actions_init(&resources.actions) == 0;
	resources.attributesReady = ::posix_spawnattr_init(&resources.attributes) == 0;
	if (!resources.actionsReady || !resources.attributesReady) {
		return false;
	}
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
	if (::posix_spawnattr_setflags(&resources.attributes, POSIX_SPAWN_CLOEXEC_DEFAULT) != 0) {
		return false;
	}
#endif // POSIX_SPAWN_CLOEXEC_DEFAULT
	const auto actionError = ::posix_spawn_file_actions_adddup2(
		&resources.actions, resources.sockets[1], STDIN_FILENO)
		|| ::posix_spawn_file_actions_adddup2(&resources.actions,
			resources.sockets[1], STDOUT_FILENO)
		|| ::posix_spawn_file_actions_addopen(
			&resources.actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0)
		|| ::posix_spawn_file_actions_addclose(&resources.actions, resources.sockets[0])
		|| ::posix_spawn_file_actions_addclose(&resources.actions, resources.sockets[1]);
	auto arguments = std::vector<std::string>{ definition.executable.string() };
	arguments.insert(arguments.end(), definition.arguments.begin(), definition.arguments.end());
	auto pointers = std::vector<char*>();
	for (auto &argument : arguments) {
		pointers.push_back(argument.data());
	}
	pointers.push_back(nullptr);
	auto locale = std::string("LC_ALL=C");
	auto path = std::string("PATH=/usr/bin:/bin");
	auto environment = std::array<char*, 3>{ locale.data(), path.data(), nullptr };
	auto process = pid_t(-1);
	const auto error = actionError ? EINVAL : ::posix_spawn(
		&process, arguments.front().c_str(), &resources.actions, &resources.attributes,
		pointers.data(), environment.data());
	if (error != 0) {
		return false;
	}
	_pid = process;
	_socket = std::exchange(resources.sockets[0], -1);
	return true;
}

bool ModuleProcess::send(std::string_view message) {
#ifdef MSG_NOSIGNAL
	constexpr auto flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#else // MSG_NOSIGNAL
	constexpr auto flags = MSG_DONTWAIT;
#endif // MSG_NOSIGNAL
	return ::send(_socket, message.data(), message.size(), flags)
		== static_cast<ssize_t>(message.size());
}

bool ModuleProcess::alive() {
	if (_pid <= 0) {
		return false;
	}
	auto status = 0;
	const auto result = ::waitpid(_pid, &status, WNOHANG);
	if (result == _pid || (result < 0 && errno != EINTR)) {
		_pid = -1;
		return false;
	}
	return true;
}

std::vector<std::string> ModuleProcess::readLines() {
	auto lines = std::vector<std::string>();
	auto data = std::array<char, 1024>();
	for (auto reads = 0; reads != 8; ++reads) {
		const auto count = ::recv(_socket, data.data(), data.size(), MSG_DONTWAIT);
		if (count <= 0) {
			break;
		}
		_buffer.append(data.data(), count);
		if (_buffer.size() > 4096) {
			_invalid = true;
			_buffer.clear();
			break;
		}
		for (auto end = _buffer.find('\n'); end != std::string::npos;
			end = _buffer.find('\n')) {
			lines.push_back(_buffer.substr(0, end));
			_buffer.erase(0, end + 1);
		}
	}
	return lines;
}

bool ModuleProcess::invalid() const {
	return _invalid;
}

} // namespace Fork::Personal
