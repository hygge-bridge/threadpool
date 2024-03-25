#include "threadpool.h"

using uLong = unsigned long long;

class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end) {}

    Any run()
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++) {
            sum += i;
        }
        return sum;
    }

private:
    int begin_;
    int end_;
};

int main() {
    {
        //线程池使用例子:使用三个线程计算1-3000的数据之和,然后汇总
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(2);
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(1000, 2000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(2000, 3000));
        uLong r1 = res1.get().cast<uLong>();
        uLong r2 = res2.get().cast<uLong>();
        uLong r3 = res3.get().cast<uLong>();
        std::cout << r1 + r2 + r3 << std::endl;
    }

    getchar();
    return 0;
}