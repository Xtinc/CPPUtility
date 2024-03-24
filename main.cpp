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
    void print_idx(int x)
    {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        std::cout << "print_custom: " << ctime(&time) << " VALUE: " << x << "\n";
    }
};

int main(int, char **)
{
    self_print t2;
    TimingWheel tw;
    std::thread t1([&tw]()
                   {
    mem_print t1{"test member function"};
    for (int i = 0; i < 50; i++)
    {
    tw.set_task(TimingWheel::HOSTING, 10_RELT, &mem_print::print_idx, t1, i); 
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } });

    for (size_t i = 0; i < 500; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    t1.join();
}
