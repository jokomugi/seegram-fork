#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
	const auto mode = (argc > 1) ? std::string(argv[1]) : "healthy";
	for (auto line = std::string(); std::getline(std::cin, line);) {
		if (line == "CHECK 1") {
			if (mode == "descriptor" && argc > 2
				&& ::fcntl(std::stoi(argv[2]), F_GETFD) != -1) {
				std::cout << "INHERITED DESCRIPTOR\n" << std::flush;
				continue;
			}
			if (mode == "secret") {
				std::cout << "TOKEN=private-message-and-api-key\n" << std::flush;
				continue;
			}
			if (mode == "hang") {
				std::this_thread::sleep_for(std::chrono::seconds(60));
			}
			if (mode == "crash") {
				std::raise(SIGKILL);
			}
			std::cout << "READY 1\n" << std::flush;
		} else if (line == "PING") {
			if (mode == "hang-running") {
				std::this_thread::sleep_for(std::chrono::seconds(60));
			}
			std::cout << "PONG\n" << std::flush;
		}
	}
}
