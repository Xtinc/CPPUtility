#include "TimingWheel.h"
#include <chrono>
#include <thread>

struct self_print
{
    void operator()(const char *str)
    {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        std::cout << "self_print: " << ctime(&time) << str << "\n";
    }
};

struct mem_print
{
    const char *str;
    void print_custom()
    {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        std::cout << "print_custom: " << ctime(&time) << " " << str << "\n";
    }
    time_t get_time()
    {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        std::cout << "print_custom: " << ctime(&time) << " " << str << "\n";
        return time;
    }
    void print_idx(int &x)
    {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        std::cout << "print_custom: " << ctime(&time) << " VALUE: " << x << "\n";
    }
};

int main(int, char **)
{
    mem_print t1{"test member function"};
    self_print t2;
    TimingWheel tw;
    int idx = 0;
    tw.set_task(TimingWheel::PERIODIC, 10_RELT, &mem_print::print_idx, t1, std::ref(idx));
    for (size_t i = 0; i < 500; i++)
    {
        ++idx;
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
