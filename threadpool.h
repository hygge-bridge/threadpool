#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

//Any类型，用于接收不同的自定义数据类型
class Any {
public:
	Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data)
		: base_(std::make_unique<Derive<T>>(data)) {}

	//将类型转换为用户指定类型
	template<typename T>
	T cast() {
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (!pd) {
			throw "类型不匹配!!!";
		}
		return pd->data_;
	}

private:
	//类型基类,类似pythonObject，所有的自定义类型都继承自它
	class Base {
	public:
		virtual ~Base() = default;
	};

	//自定义类型派生类，用户的具体类型数据存放在这里
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data)
			: data_(data) {}
		T data_;
	};

private:
	std::unique_ptr<Base> base_; //利用基类指针指向派生类对象，从而实现任务返回自定义类型
};

//信号量类
class Semaphore {
public:
	Semaphore(int limit = 0);

	~Semaphore();

	//减少一个信号量资源
	void wait();

	//增加一个信号量资源
	void post();

private:
	int resourceLimit_; //资源计数
	std::mutex mtx_;
	std::condition_variable cond_;
	//判断条件变量是否被析构了，linux不会为我析构资源，所以这里需要来判断一下
	std::atomic_bool isExit_;
};

class Task;
//返回值类，用于获取不同类型的返回值
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);

	~Result() = default;

	//获取任务的返回值
	void setVal(Any any);

	//返回任务执行完的返回值
	Any get();

private:
	Any any_; //可以保存不同任务的返回值
	Semaphore sem_; //线程通信
	std::shared_ptr<Task> task_; //保存任务，用于获取任务返回值
	std::atomic_bool isValid_; //判断返回值是否有效
};

//线程池模式
enum class PoolMode {
	MODE_FIXED, //固定线程个数
	MODE_CACHED, //可变线程个数
};

//任务抽象基类，所有任务就需要继承该基类
class Task {
public:
	//设置Result
	void setResult(Result* result);

	//执行任务并且返回任务执行结束的返回值
	void exec();

	//用于传入的任务都必须重写这个方法，从而实现用户自定义类型
	virtual Any run() = 0;

private:
	Result* result_; //不可以用强智能指针，防止循环计数
};

//线程类
class Thread {
public:
	//利用function接收线程函数类型
	using threadFunc = std::function<void(int)>;

	Thread(threadFunc func);

	~Thread();

	//返回线程id
	int getId() const;

	//启动线程
	void start();
private:
	threadFunc func_; //线程函数对象
	static int generateId_; //用于产生id
	int threadId_; //线程id
};

//线程池类
class ThreadPool {
public:
	ThreadPool();

	~ThreadPool();

	//设置线程池模式
	void setMode(PoolMode poolMode);

	//设置任务数量最大阈值
	void setTaskMaxThreshold(int threshold);

	//设置线程数量最大阈值
	void setThreadMaxThreshold(int threshold);

	//开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());

	//提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	//禁止线程池拷贝
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//检查线程池是否已经启动
	bool checkPoolRunning() const;

	//线程函数
	void threadFunc(int threadId);

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表
	int initThreadSize_; //初始化线程数量
	int threadMaxThreshold_; //线程数量上限阈值
	std::atomic_int currThreadSize_; //当前线程数量
	std::atomic_int idleThreadSize_; //空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
	std::atomic_int taskQueSize_; //当前任务的数量
	int taskQueMaxThreshold_; //任务数量最大阈值

	std::mutex taskMutex_; //保障任务队列线程安全的锁
	std::condition_variable notEmpty_; //表示任务队列不空，可以提取任务
	std::condition_variable notFull_; //表示任务队列不满，可以添加任务

	PoolMode poolMode_; //线程池模式
	std::atomic_bool isPoolRunning_; //线程池运行状态
	std::condition_variable exited_; //表示线程池准备退出
};

#endif