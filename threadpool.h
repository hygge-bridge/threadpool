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

//Any���ͣ����ڽ��ղ�ͬ���Զ�����������
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

	//������ת��Ϊ�û�ָ������
	template<typename T>
	T cast() {
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (!pd) {
			throw "���Ͳ�ƥ��!!!";
		}
		return pd->data_;
	}

private:
	//���ͻ���,����pythonObject�����е��Զ������Ͷ��̳�����
	class Base {
	public:
		virtual ~Base() = default;
	};

	//�Զ������������࣬�û��ľ����������ݴ��������
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data)
			: data_(data) {}
		T data_;
	};

private:
	std::unique_ptr<Base> base_; //���û���ָ��ָ����������󣬴Ӷ�ʵ�����񷵻��Զ�������
};

//�ź�����
class Semaphore {
public:
	Semaphore(int limit = 0);

	~Semaphore();

	//����һ���ź�����Դ
	void wait();

	//����һ���ź�����Դ
	void post();

private:
	int resourceLimit_; //��Դ����
	std::mutex mtx_;
	std::condition_variable cond_;
	//�ж����������Ƿ������ˣ�linux����Ϊ��������Դ������������Ҫ���ж�һ��
	std::atomic_bool isExit_;
};

class Task;
//����ֵ�࣬���ڻ�ȡ��ͬ���͵ķ���ֵ
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);

	~Result() = default;

	//��ȡ����ķ���ֵ
	void setVal(Any any);

	//��������ִ����ķ���ֵ
	Any get();

private:
	Any any_; //���Ա��治ͬ����ķ���ֵ
	Semaphore sem_; //�߳�ͨ��
	std::shared_ptr<Task> task_; //�����������ڻ�ȡ���񷵻�ֵ
	std::atomic_bool isValid_; //�жϷ���ֵ�Ƿ���Ч
};

//�̳߳�ģʽ
enum class PoolMode {
	MODE_FIXED, //�̶��̸߳���
	MODE_CACHED, //�ɱ��̸߳���
};

//���������࣬�����������Ҫ�̳иû���
class Task {
public:
	//����Result
	void setResult(Result* result);

	//ִ�������ҷ�������ִ�н����ķ���ֵ
	void exec();

	//���ڴ�������񶼱�����д����������Ӷ�ʵ���û��Զ�������
	virtual Any run() = 0;

private:
	Result* result_; //��������ǿ����ָ�룬��ֹѭ������
};

//�߳���
class Thread {
public:
	//����function�����̺߳�������
	using threadFunc = std::function<void(int)>;

	Thread(threadFunc func);

	~Thread();

	//�����߳�id
	int getId() const;

	//�����߳�
	void start();
private:
	threadFunc func_; //�̺߳�������
	static int generateId_; //���ڲ���id
	int threadId_; //�߳�id
};

//�̳߳���
class ThreadPool {
public:
	ThreadPool();

	~ThreadPool();

	//�����̳߳�ģʽ
	void setMode(PoolMode poolMode);

	//�����������������ֵ
	void setTaskMaxThreshold(int threshold);

	//�����߳����������ֵ
	void setThreadMaxThreshold(int threshold);

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	//�ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//��ֹ�̳߳ؿ���
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//����̳߳��Ƿ��Ѿ�����
	bool checkPoolRunning() const;

	//�̺߳���
	void threadFunc(int threadId);

private:
	//std::vector<std::unique_ptr<Thread>> threads_; //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; //�߳��б�
	int initThreadSize_; //��ʼ���߳�����
	int threadMaxThreshold_; //�߳�����������ֵ
	std::atomic_int currThreadSize_; //��ǰ�߳�����
	std::atomic_int idleThreadSize_; //�����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; //�������
	std::atomic_int taskQueSize_; //��ǰ���������
	int taskQueMaxThreshold_; //�������������ֵ

	std::mutex taskMutex_; //������������̰߳�ȫ����
	std::condition_variable notEmpty_; //��ʾ������в��գ�������ȡ����
	std::condition_variable notFull_; //��ʾ������в����������������

	PoolMode poolMode_; //�̳߳�ģʽ
	std::atomic_bool isPoolRunning_; //�̳߳�����״̬
	std::condition_variable exited_; //��ʾ�̳߳�׼���˳�
};

#endif