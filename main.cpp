#include "TimingWheel.h"
#include <chrono>
#include <thread>

int main(int, char **)
{
    TimingWheel<int> tw(1000);
    tw.set_task(1, 1_ABST);
    tw.set_task(2, 1_ABST);
    tw.set_task(3, 4_ABST);
    tw.set_task(4, 2_ABST);
    tw.set_task(5, 3_ABST);
    tw.set_task(6, 3_ABST);
    tw.set_task(7, 3_ABST);
    tw.set_task(8, 5_ABST);
    tw.set_task(9, 8_ABST);
    for (size_t i = 0; i < 10; i++)
    {
        tw.go();
    }
    tw.set_task(5, 0_RELT);
    tw.set_task(5, 0_RELT);
    tw.set_task(6, 1_RELT);
    tw.set_task(7, 256_ABST);
    tw.set_task(7, 1048576_ABST);
    tw.set_task(8, 32223_RELT);
    tw.set_task(9, 112983674_ABST);
    for (size_t i = 0; i < std::numeric_limits<uint32_t>::max(); i++)
    {
        tw.go();
    }
}
