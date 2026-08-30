#include "Runtime.hpp"

#include <atomic>
#include <memory>
#include <utility>
#include <iostream>

namespace renderer {

namespace {
Configuration activeConfiguration;
std::atomic<std::shared_ptr<const Runtime::LogCallback>> logCallback;
}

Configuration& Runtime::configuration() noexcept {
	return activeConfiguration;
}

void Runtime::configure(Configuration&& configuration) noexcept {
	activeConfiguration = std::move(configuration);
}

void Runtime::setLogCallback(const LogCallback& callback) noexcept {
	try {
		logCallback.store(
			std::make_shared<const Runtime::LogCallback>(callback),
			std::memory_order_release);
	}
	catch (...) {
		log(LogLevel::error, "Failed to set log callback");
	}
}

// Should swap to a argument approatch to avoid heap allocation
void Runtime::log(LogLevel level, std::string_view message) noexcept {
	if (logCallback.load(std::memory_order_acquire)) {
		try {
			logCallback.load(std::memory_order_acquire)->operator()(level, message);
			return;
		}
		catch (...) {
		}
	}

	std::string_view prefix = "[renderer] ";
	if (level == LogLevel::warning) {
		prefix = "[renderer warning] ";
	}
	else if (level == LogLevel::error) {
		prefix = "[renderer error] ";
	}

	try {
		std::cerr << prefix << message << "\n";
	}
	catch (...) {
	}
}

}
