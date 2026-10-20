#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		return 2;
	}
	const auto id = std::string(argv[1]);
	if (id != "diagnostics" && id != "slightly-redesigned") {
		return 2;
	}
	for (auto line = std::string(); std::getline(std::cin, line);) {
		if (line == "CHECK 1") {
			std::cout << "READY 1\n" << std::flush;
		} else if (line == "PING") {
			std::cout << "PONG\n" << std::flush;
		} else {
			return 3;
		}
	}
}
