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
};

int main(int, char **)
{
    mem_print t1{"test member function"};
    self_print t2;
    TimingWheel tw;
    tw.set_task(TimingWheel::HOSTING, 1_ABST, &mem_print::print_custom, t1);
    tw.set_task(TimingWheel::HOSTING, 1_ABST, t2, "32");
    tw.set_task(TimingWheel::HOSTING, 4_ABST, t2, "33");
    // tw.set_task(TimingWheel::CYCLE, 2_ABST, t2, "34");
    tw.set_task(TimingWheel::INTERACT, 3_ABST, t2, "interact");
    tw.set_task(TimingWheel::HOSTING, 8_ABST, &mem_print::print_custom, t1);
    tw.set_task(TimingWheel::HOSTING, 10_ABST, &mem_print::print_custom, t1);
    tw.set_task(TimingWheel::HOSTING, 20_ABST, &mem_print::get_time, t1);
    auto fff = tw.set_task(TimingWheel::INTERACT, 10_ABST, &mem_print::print_custom, t1);
    for (size_t i = 0; i < 10; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    tw.set_task(TimingWheel::HOSTING, 0_RELT, &mem_print::print_custom, t1);
    tw.set_task(TimingWheel::HOSTING, 0_RELT, t2, "45");
    tw.set_task(TimingWheel::HOSTING, 1_RELT, t2, "46");
    tw.set_task(TimingWheel::INTERACT, 256_ABST, []()
                { printf("256_ABST\n"); });
    tw.set_task(TimingWheel::HOSTING, 209_ABST, []()
                { printf("size of lattice: %zu\n", sizeof(TimingWheel)); });
    tw.set_task(TimingWheel::PERIODIC, 5_RELT, []()
                { printf("print period!\n"); });
    tw.set_task(TimingWheel::CYCLES, 10_RELT, 10, []()
                { printf("run cycles\n"); });
    for (size_t i = 0; i < 500; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
