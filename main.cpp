#include "TimingWheel.h"
#include <chrono>
#include <thread>

int main(int, char **)
{
    TimingWheel<int> tw(1000);
    tw.set_task(1, 1);
    tw.set_task(2, 1);
    tw.set_task(3, 4);
    tw.set_task(4, 2);
    tw.set_task(5, 3);
    tw.set_task(6, 3);
    tw.set_task(7, 3);
    tw.set_task(8, 5);
    tw.set_task(9, 8);
    for (size_t i = 0; i < 10; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    tw.set_task(5, 11);
    tw.set_task(6, 12);
    tw.set_task(7, 13);
    tw.set_task(8, 13);
    tw.set_task(9, 14);

    for (size_t i = 0; i < 20; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
