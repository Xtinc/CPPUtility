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
        printf("print_custom: %sVALUE: %d\n", ctime(&time), x);
    }
};

int main(int, char **)
{
    self_print t2;
    TimingWheel tw;
    t2("start!");
    for (size_t i = 0; i < 2; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::thread t1([&tw]()
                   {
        mem_print p1{"cus"};
        tw.set_task(TimingWheel::CYCLES,3_ABST,3,&mem_print::get_time,p1); });
    for (size_t i = 0; i < 12; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    t1.join();
}
