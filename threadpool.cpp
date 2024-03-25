#include "threadpool.h"

//可以根据电脑配置自己修改，线程池也提供了对应方法
const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位秒

//Semaphore---------------------------------------------------
Semaphore::Semaphore(int limit)
	: resourceLimit_(limit)
	, isExit_(false) {}

Semaphore::~Semaphore() {
	isExit_ = true;
}

void Semaphore::wait() {
	if (isExit_) {
		return;
	}
	std::unique_lock<std::mutex> lock(mtx_);
	cond_.wait(lock, [&]()->bool { return resourceLimit_ > 0; });
	--resourceLimit_;
}

void Semaphore::post() {
	if (isExit_) {
		return;
	}
	std::unique_lock<std::mutex> lock(mtx_);
	++resourceLimit_;
	cond_.notify_all();
}

//Result------------------------------------------------------
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: task_(task)
	, isValid_(isValid) {
	task_->setResult(this);
}

void Result::setVal(Any any) {
	any_ = std::move(any);
	sem_.post();
}

Any Result::get() {
	if (!isValid_) {
		return Any();
	}
	sem_.wait();
	return std::move(any_);
}

//Task---------------------------------------------------------
void Task::setResult(Result* result) {
	result_ = result;
}

void Task::exec() {
	if (result_) {
		result_->setVal(run());
	}
}

//Thread-------------------------------------------------------
int Thread::generateId_ = 0;

Thread::Thread(threadFunc func)
	: func_(func)
	, threadId_(generateId_++) {}

Thread::~Thread() {}

int Thread::getId() const {
	return threadId_;
}

void Thread::start() {
	std::thread t(func_, threadId_);
	t.detach();
}

//ThreadPool----------------------------------------------------
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, threadMaxThreshold_(THREAD_MAX_THRESHOLD)
	, currThreadSize_(0)
	, idleThreadSize_(0)
	, taskQueSize_(0)
	, taskQueMaxThreshold_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false) {}

ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	notEmpty_.notify_all();
	std::unique_lock<std::mutex> lock(taskMutex_);
	exited_.wait(lock, [&]()->bool { return threads_.empty(); });
}

void ThreadPool::setMode(PoolMode poolMode) {
	if (checkPoolRunning()) {
		return;
	}
	poolMode_ = poolMode;
}

void ThreadPool::setTaskMaxThreshold(int threshold) {
	if (checkPoolRunning()) {
		return;
	}
	taskQueMaxThreshold_ = threshold;
}

void ThreadPool::setThreadMaxThreshold(int threshold) {
	if (checkPoolRunning()) {
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadMaxThreshold_ = threshold;
	}
}

void ThreadPool::start(int initThreadSize) {
	isPoolRunning_ = true;
	initThreadSize_ = initThreadSize;
	currThreadSize_ = initThreadSize;
	idleThreadSize_ = initThreadSize;

	//为了实现线程之间的公平性，所以创建完所有线程后，再启动线程
	for (int i = 0; i < initThreadSize_; ++i) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	for (int i = 0; i < initThreadSize_; ++i) {
		threads_[i]->start();
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task> task) {
	std::unique_lock<std::mutex> lock(taskMutex_);
	//如果阻塞一秒钟还没有提交成功，就直接返回提交失败
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < static_cast<size_t>(taskQueMaxThreshold_); })) {
		std::cerr << "任务队列已满，提交任务失败" << std::endl;
		return Result(task, false);
	}
	taskQue_.emplace(task);
	++taskQueSize_;
	notEmpty_.notify_all();

	//cached模式,动态增加线程数量
	if (poolMode_ == PoolMode::MODE_CACHED
		&& idleThreadSize_ < taskQueSize_
		&& currThreadSize_ < threadMaxThreshold_) {
		std::cout << "创建新线程" << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		++currThreadSize_;
		++idleThreadSize_;
	}

	return Result(task);
}

inline bool ThreadPool::checkPoolRunning() const {
	return isPoolRunning_;
}

void ThreadPool::threadFunc(int threadId) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskMutex_);
			std::cout << "线程" << std::this_thread::get_id() << "正在获取任务..." << std::endl;
			while (taskQue_.empty()) {
				if (!checkPoolRunning()) {
					//回收线程池的线程
					threads_.erase(threadId);
					exited_.notify_all();
					std::cout << "线程" << std::this_thread::get_id() << "回收" << std::endl;
					return;
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						//当线程闲置时间过长，就应该回收线程
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > THREAD_MAX_IDLE_TIME
							&& currThreadSize_ > initThreadSize_) {
							threads_.erase(threadId);
							--idleThreadSize_;
							--currThreadSize_;
							std::cout << "cached模式下的线程" << std::this_thread::get_id() << "回收" << std::endl;
						}
					}
				}
				else {
					notEmpty_.wait(lock);
				}
			}

			std::cout << "线程" << std::this_thread::get_id() << "获取任务成功！！！" << std::endl;

			--idleThreadSize_;
			task = taskQue_.front();
			taskQue_.pop();
			--taskQueSize_;
			if (!taskQue_.empty()) {
				notEmpty_.notify_all();
			}
			notFull_.notify_all();
		}
		if (task) {
			task->exec();
		}
		++idleThreadSize_;
		lastTime = std::chrono::high_resolution_clock().now();
	}
}

