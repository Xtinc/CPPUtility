#include "TimingWheel.h"
#include <chrono>
#include <thread>

struct testccc
{
    int a;
    int tst()
    {
        printf("%d\n", a);
        return 4;
    }
};

void print_t(int a)
{
    printf("%d\n", a);
}

int main(int, char **)
{
    testccc ccc{12};
    TimingWheel tw;
    tw.set_task(TimingWheel::HOSTING, 1_ABST, print_t, 1);
    tw.set_task(TimingWheel::HOSTING, 1_ABST, print_t, 2);
    tw.set_task(TimingWheel::HOSTING, 4_ABST, print_t, 3);
    tw.set_task(TimingWheel::HOSTING, 2_ABST, print_t, 4);
    tw.set_task(TimingWheel::HOSTING, 3_ABST, print_t, 5);
    tw.set_task(TimingWheel::HOSTING, 3_ABST, print_t, 6);
    tw.set_task(TimingWheel::HOSTING, 3_ABST, print_t, 7);
    tw.set_task(TimingWheel::HOSTING, 5_ABST, print_t, 8);
    tw.set_task(TimingWheel::HOSTING, 8_ABST, print_t, 9);
    tw.set_task(TimingWheel::HOSTING, 19_ABST, &testccc::tst, ccc);
    tw.set_task(TimingWheel::HOSTING, 10_ABST, &testccc::tst, ccc);
    auto fff = tw.set_task(TimingWheel::INTERACT, 10_ABST, &testccc::tst, ccc);
    for (size_t i = 0; i < 10; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    tw.set_task(TimingWheel::HOSTING, 0_RELT, print_t, 11);
    tw.set_task(TimingWheel::HOSTING, 0_RELT, print_t, 11);
    tw.set_task(TimingWheel::HOSTING, 1_RELT, print_t, 12);
    tw.set_task(TimingWheel::INTERACT, 256_ABST, []()
                { printf("256_ABST\n"); });
    tw.set_task(TimingWheel::HOSTING, 1048576_ABST, []()
                { printf("1048576_ABST\n"); });
    tw.set_task(TimingWheel::CYCLE, 5_RELT, []()
                { printf("print period!\n"); });
    for (size_t i = 0; i < 50; i++)
    {
        tw.go();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
