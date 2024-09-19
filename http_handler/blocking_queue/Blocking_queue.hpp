#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class BlockingQueue {
private:
	std::mutex mutex_;
	std::condition_variable cond_var;
	std::queue<T> que;

public:
	BlockingQueue() = default;
	~BlockingQueue() = default;

	BlockingQueue(const BlockingQueue&) = delete;
	BlockingQueue& operator= (const BlockingQueue&) = delete;	

	void push(T element) {
		std::unique_lock<std::mutex> lock_(mutex_);
		que.push(element);
		cond_var.notify_one();
	}

	T pop() {
		std::unique_lock<std::mutex> lock_(mutex_);
		cond_var.wait(lock_, [this] { return !que.empty(); });
		T tmp_element = que.front();
		que.pop();
		return tmp_element;
	}
};

#endif // BLOCKING_QUEUE_H