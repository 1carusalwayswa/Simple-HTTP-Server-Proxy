#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

// BlockingQueue is a thread-safe queue.
// Using mutex and condition_variable to implement the blocking queue.
// So we don't need to use sleep to wait for the queue to be empty.
// It provides two functions: push and pop.
// If the queue is empty, the pop function will block the thread.
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

	// push an element into the queue
	// notify the waiting thread
	void push(T element) {
		std::unique_lock<std::mutex> lock_(mutex_);
		que.push(element);
		// notify the waiting thread
		cond_var.notify_one();
	}

	// pop an element from the queue
	// if the queue is empty, the thread will be blocked.
	T pop() {
		std::unique_lock<std::mutex> lock_(mutex_);
		// if the queue is empty, the thread will be blocked.
		// and wait for the condition_variable to be notified.
		// when the queue is not empty, the thread will be woken up.
		cond_var.wait(lock_, [this] { return !que.empty(); });
		T tmp_element = que.front();
		que.pop();
		return tmp_element;
	}
};

#endif // BLOCKING_QUEUE_H