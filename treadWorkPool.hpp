#pragma once

#include "helpers.hpp"

#include <vector>
#include <mutex>
#include <thread>

namespace vulkan {
template <typename PoolState, typename ThreadState>
class TreadWorkPool {
public:
	using StartupFn = std::function<ThreadState(PoolState&)>;
	using UpdateFn = std::function<bool(PoolState&, ThreadState&)>;
	using CleanupFn = std::function<void(PoolState&, ThreadState&)>;

	TreadWorkPool(size_t threadCount, PoolState poolState, UpdateFn update, StartupFn startup = nullptr, CleanupFn cleanup = nullptr);
	~TreadWorkPool();

	void start() {
		for (Thread& thread : _threads)
			thread.thread = std::thread(&TreadWorkPool::workerStartup, this);
	}

	bool hasCompleted() const {
		for (const Thread& thread : _threads)
			if (!thread.completed)
				return false;
		return true;
	}
	void waitForWork() {
		assert(std::find_if(_threads.begin(), _threads.end(), [](const Thread& thread) { return !thread.thread.joinable(); }) == _threads.end() 
			&& "Some threads are not joinable");
		for (Thread& thread : _threads)
			thread.thread.join();
	}

	bool hasExceptions() {
		std::lock_guard<std::mutex> lock(_exMutex);
		return _threadException != nullptr;
	}
	void getExceptions() {
		std::lock_guard<std::mutex> lock(_exMutex);
		if (_threadException)
			std::rethrow_exception(_threadException);
	}

	void forceStop() noexcept {
		_forceStop = true;
		for (Thread& thread : _threads)
			thread.thread.join();
	}

private:
	void workerStartup() {
		ThreadState state;
		try {
			if (_startupFn)
				state = _startupFn(_poolState);
			while (!_forceStop) {
				if (!_updateFn(_poolState, state))
					break;
			}
			if (_cleanupFn)
				_cleanupFn(_poolState, state);

			std::thread::id thisId = std::this_thread::get_id();
			for (Thread& thread : _threads) {
				if (thread.thread.get_id() == thisId) {
					thread.completed = true;
					return;
				}
			}
		}
		catch (...) {
			std::lock_guard<std::mutex> lock(_exMutex);
			_threadException = std::current_exception();
		}
	}

	struct Thread {
		std::thread thread;
		std::atomic<bool> completed = false;
	};

	std::vector<Thread> _threads;

	std::exception_ptr _threadException;
	std::mutex _exMutex;

	StartupFn _startupFn;
	UpdateFn _updateFn;
	CleanupFn _cleanupFn;

	PoolState _poolState;
	std::atomic<bool> _forceStop = false;
};

template <typename PoolState, typename ThreadState>
inline TreadWorkPool<PoolState, ThreadState>::TreadWorkPool(size_t threadCount, PoolState poolState, UpdateFn update, StartupFn startup, CleanupFn cleanup)
	: _threads(threadCount)
	, _poolState(poolState)
	, _updateFn(update)
	, _startupFn(startup)
	, _cleanupFn(cleanup)
{
	assert(_updateFn && "Update function cannot be null");
	assert(threadCount > 0 && "Thread count must be greater than 0");
}
template <typename PoolState, typename ThreadState>
inline TreadWorkPool<PoolState, ThreadState>::~TreadWorkPool() {
	if (!_threads[0].thread.joinable()) {
		assert(std::find_if(_threads.begin(), _threads.end(), [](const Thread& thread) { return thread.thread.joinable(); }) == _threads.end() 
			&& "Some threads are joinable, but not all. This is a bug, as all threads should be joinable or none should be joinable");
		return;
	}
	waitForWork();
}
}