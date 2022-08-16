#include <vector>
#include <memory>
#include <mutex>

template <typename T, unsigned int CAPACITY, bool THREAD_SAFE>
class RingBuffer {
public:
	RingBuffer() : idx(0), end_idx(0), full(false) {
		data.resize(CAPACITY);
	}

	unsigned int get_capacity() {
		return CAPACITY;
	}

	bool is_empty() {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return is_empty_impl();
		} else {
			return is_empty_impl();
		}
	}

private:

	bool is_empty_impl() {
		return idx == end_idx && !full;
	}

public:

	bool has_space() {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return has_space_impl();
		} else {
			return has_space_impl();
		}
	}

private:

	bool has_space_impl() {
		return !(idx == end_idx && full);
	}

public:

	int get_remaining_space() {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return get_remaining_space_impl();
		} else {
			return get_remaining_space_impl();
		}
	}

private:

	int get_remaining_space_impl() {
		if (idx == end_idx) {
			return full ? CAPACITY : 0;
		} else if (idx < end_idx) {
			return end_idx - idx;
		} else {
			return (end_idx + CAPACITY) - idx;
		}
	}

public:

	bool push(const T &value) {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return push_impl(value);
		} else {
			return push_impl(value);
		}
	}

private:
	bool push_impl(const T &value) {
		if (idx == end_idx && full) {
			return false;
		} else {
			data[end_idx] = value;
			end_idx = (end_idx + 1) % CAPACITY;
			if (idx == end_idx) {
				full = true;
			}
			return true;
		}
	}

public:

	bool push(T &&value) {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return push_impl(std::forward<T>(value));
		} else {
			return push_impl(std::forward<T>(value));
		}
	}

private:

	bool push_impl(T &&value) {
		if (idx == end_idx && full) {
			return false;
		} else {
			data[end_idx] = std::forward<T>(value);
			end_idx = (end_idx + 1) % CAPACITY;
			if (idx == end_idx) {
				full = true;
			}
			return true;
		}
	}

public:

	T* top() {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			return top_impl();
		} else {
			return top_impl();
		}
	}

	using LockedTopLock = std::shared_ptr<std::lock_guard<std::mutex>>;
	using LockedTopTuple = std::tuple<T*, LockedTopLock>;

	LockedTopTuple locked_top() {
		LockedTopLock lock = std::make_shared<std::lock_guard<std::mutex>>(mutex);
		return {top_impl(), std::move(lock)};
	}

private:
	T* top_impl() {
		if (idx == end_idx && !full) {
			return nullptr;
		} else {
			return &data[idx];
		}
	}

public:

	void pop(void(*cleanup_fn)(T *value)) {
		if (THREAD_SAFE) {
			std::lock_guard<std::mutex> lock(mutex);
			pop_impl(cleanup_fn);
		} else {
			pop_impl(cleanup_fn);
		}
	}

private:

	void pop_impl(void(*cleanup_fn)(T *value)) {
		if (idx == end_idx && !full) {
			return;
		}
		if (cleanup_fn) {
			cleanup_fn(&data[idx]);
		}
		idx = (idx + 1) % CAPACITY;
		if (full) {
			full = false;
		}
	}

	std::mutex mutex;
	std::vector<T> data;
	unsigned int idx;
	unsigned int end_idx;
	bool full;

};
