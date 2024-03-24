#ifndef TIMING_WHEEL_HEADER
#define TIMING_WHEEL_HEADER

#include <type_traits>
#include <functional>
#include <future>
#include <mutex>
#include <atomic>
#include <iostream>
// only for debug
#include <iomanip>

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(__cpp_lib_is_invocable)
#define TW_SAFE_INVOKE_RT std::invoke_result
#else
#define TW_SAFE_INVOKE_RT std::result_of
#endif
#else
#define TW_SAFE_INVOKE_RT std::result_of
#endif

struct AbsoluteTimeTick
{
    constexpr AbsoluteTimeTick(uint32_t t) : tick(t){};
    uint32_t tick;
};

struct RelativeTimeTick
{
    constexpr RelativeTimeTick(uint32_t t) : tick(t){};
    uint32_t tick;
};

constexpr AbsoluteTimeTick operator"" _ABST(unsigned long long t)
{
    return {static_cast<uint32_t>(t)};
}

constexpr RelativeTimeTick operator"" _RELT(unsigned long long t)
{
    return {static_cast<uint32_t>(t)};
}

class TimingWheel
{
    struct HOSTING_T
    {
    };
    struct PERIODIC_T
    {
    };
    struct INTERACT_T
    {
    };
    struct CYCLIC_T
    {
    };

    using func_obj = std::function<void()>;

    class Worker
    {
        constexpr static auto MAX_SIZE = 101;

    public:
        Worker() : front(1), rear(0), stop(false), thd(&Worker::do_work, this) {}
        ~Worker()
        {
            {
                std::unique_lock<std::mutex> lck(mtx);
                stop = true;
            }
            cond.notify_one();
            thd.join();
        }

        bool submit(func_obj &&obj)
        {
            {
                std::unique_lock<std::mutex> lck(mtx);
                if (stop)
                {
                    return false;
                }
                if ((rear + 2) % MAX_SIZE == front)
                {
                    return false;
                }
                rear = (rear + 1) % MAX_SIZE;
                queue[rear] = obj;
            }
            cond.notify_one();
            return true;
        }

    private:
        void do_work()
        {
            for (;;)
            {
                func_obj func;
                {
                    std::unique_lock<std::mutex> lck(mtx);
                    cond.wait(lck, [this]()
                              { return stop || (length() != 0); });
                    if (stop && length() == 0)
                    {
                        return;
                    }
                    func = std::move(queue[front]);
                    front = (front + 1) % MAX_SIZE;
                }
                func();
            }
        }

        int length() const
        {
            return ((rear + MAX_SIZE) - front + 1) % MAX_SIZE;
        }

    private:
        func_obj queue[MAX_SIZE];
        int front;
        int rear;
        bool stop;
        std::thread thd;
        std::mutex mtx;
        std::condition_variable cond;
    };

    constexpr static uint32_t TWR_BITS = 8;
    constexpr static uint32_t TWN_BITS = 6;
    constexpr static uint32_t TWR_SIZE = 1 << TWR_BITS;
    constexpr static uint32_t TWN_SIZE = 1 << TWN_BITS;
    constexpr static uint32_t TWR_MASK = TWR_SIZE - 1;
    constexpr static uint32_t TWN_MASK = TWN_SIZE - 1;

    constexpr static auto FST_IDX(uint32_t t)
    {
        return t & TWR_MASK;
    }

    constexpr static auto NTH_IDX(uint32_t t, size_t n)
    {
        return (t >> (TWR_BITS + n * TWN_BITS)) & TWN_MASK;
    }

    struct lattice
    {
        lattice *prev;
        lattice *next;
        uint32_t expired;
        uint32_t lifespan;
        func_obj ele;

        static void set_init(lattice *node)
        {
            node->prev = node;
            node->next = node;
            node->expired = 0;
            node->lifespan = 0;
        }

        void *operator new(size_t)
        {
            if (freelist == nullptr)
            {
                return ::new lattice;
            }
            auto *temp = freelist;
            freelist = freelist->next;
            auto head = freelist;
            std::cout << "freelist: " << (void *)head;
            while (head != nullptr)
            {
                head = head->next;
                std::cout << " -> " << (void *)head;
            }
            std::cout << "\n";
            return temp;
        }

        void operator delete(void *ptr)
        {
            ((lattice *)ptr)->next = freelist;
            freelist = (lattice *)ptr;
        }

    private:
        static thread_local lattice *freelist;
    };

    using tw_fst_t = lattice[TWR_SIZE];
    using tw_nth_t = lattice[TWN_SIZE];

public:
    constexpr static HOSTING_T HOSTING{};
    constexpr static PERIODIC_T PERIODIC{};
    constexpr static INTERACT_T INTERACT{};
    constexpr static CYCLIC_T CYCLES{};

    TimingWheel(uint32_t current_time = 0)
        : currtick(current_time)
    {
        for (size_t i = 0; i < TWR_SIZE; i++)
        {
            lattice::set_init(tw_1st + i);
        }
        for (size_t j = 0; j < 4; j++)
        {
            auto *temp = tw_nth[j];
            for (size_t i = 0; i < TWN_SIZE; i++)
            {
                lattice::set_init(temp + i);
            }
        }
    }

    template <class Fn, class... Args>
    void set_task(HOSTING_T, AbsoluteTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, time.tick, 0,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice(time.tick, temp);
    }

    template <class Fn, class... Args>
    void set_task(PERIODIC_T, AbsoluteTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, time.tick, 0xFFFFFFFF,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice((currtick.load(std::memory_order_acquire) / time.tick + 1) * time.tick, temp);
    }

    template <class Fn, class... Args>
    void set_task(CYCLIC_T, AbsoluteTimeTick time, uint32_t cycles, Fn &&Fx, Args &&...Ax)
    {
        if (cycles == 0)
        {
            cycles = 1;
        }
        auto temp = new lattice{nullptr, nullptr, time.tick, cycles - 1,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice((currtick.load(std::memory_order_acquire) / time.tick + 1) * time.tick, temp);
    }

    template <class Fn, class... Args>
    auto set_task(INTERACT_T, AbsoluteTimeTick time, Fn &&Fx, Args &&...Ax)
        -> std::future<typename TW_SAFE_INVOKE_RT<Fn(Args...)>::type>
    {
        using rt = typename TW_SAFE_INVOKE_RT<Fn(Args...)>::type;
        auto task_ptr =
            std::make_shared<std::packaged_task<rt()>>(std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...));
        auto temp = new lattice{nullptr, nullptr, time.tick, 0, [task_ptr]()
                                { (*task_ptr)(); }};
        insert_lattice(time.tick, temp);
        return task_ptr->get_future();
    }

    template <class Fn, class... Args>
    void set_task(HOSTING_T, RelativeTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        if (time.tick == 0)
        {
            time.tick = 1;
        }
        auto real_tick = currtick.load(std::memory_order_acquire) + time.tick;
        auto temp = new lattice{nullptr, nullptr, real_tick, 0,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice(real_tick, temp);
    }

    template <class Fn, class... Args>
    auto set_task(PERIODIC_T, RelativeTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        if (time.tick == 0)
        {
            time.tick = 1;
        }
        auto real_tick = currtick.load(std::memory_order_acquire) + time.tick;
        auto temp = new lattice{nullptr, nullptr, real_tick, 0XFFFFFFFF,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice(real_tick, temp);
    }

    template <class Fn, class... Args>
    auto set_task(CYCLIC_T, RelativeTimeTick time, uint32_t cycles, Fn &&Fx, Args &&...Ax)
    {
        if (time.tick == 0)
        {
            time.tick = 1;
        }
        if (cycles == 0)
        {
            cycles = 1;
        }
        auto real_tick = currtick.load(std::memory_order_acquire) + time.tick;
        auto temp = new lattice{nullptr, nullptr, real_tick, cycles - 1,
                                std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)};
        insert_lattice(real_tick, temp);
    }

    template <class Fn, class... Args>
    auto set_task(INTERACT_T, RelativeTimeTick time, Fn &&Fx, Args &&...Ax)
        -> std::future<typename TW_SAFE_INVOKE_RT<Fn(Args...)>::type>
    {
        using rt = typename TW_SAFE_INVOKE_RT<Fn(Args...)>::type;
        if (time.tick == 0)
        {
            time.tick = 1;
        }
        auto real_tick = currtick.load(std::memory_order_acquire) + time.tick;
        auto task_ptr = std::make_shared<std::packaged_task<rt()>>(std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...));
        auto temp = new lattice{nullptr, nullptr, real_tick, 0, [task_ptr]()
                                { (*task_ptr)(); }};
        insert_lattice(real_tick, temp);
        return task_ptr->get_future();
    }

    void go()
    {
        auto ticks = currtick.fetch_add(1, std::memory_order_release) + 1;
        auto index = FST_IDX(ticks);
        std::unique_lock<std::mutex> lck(tw_mtx);
        if (index == 0)
        {
            uint32_t i = 0;
            uint32_t tpx = 0;
            do
            {
                tpx = NTH_IDX(ticks, i);
                move_lattice_cascade(tw_nth[i] + tpx);

            } while (tpx == 0 && ++i < 4);
        }
        lattice *head = tw_1st + index;
        while (head != head->next)
        {
            lattice *temp = head->next;
            workers[0].submit(std::move(temp->ele));
            temp->next->prev = temp->prev;
            temp->prev->next = temp->next;

            if (temp->lifespan != 0)
            {
                auto *head = calculate_lattice(temp->expired);
                temp->lifespan--;
                temp->prev = head->prev;
                temp->next = head;
                temp->prev->next = temp;
                head->prev = temp;
            }
            else
            {
                delete temp;
            }
        }
    }

    uint32_t now() const
    {
        return currtick.load(std::memory_order_acquire);
    }

    // only for debug
    void print_self()
    {
        std::cout << "sizeof lattice: " << sizeof(lattice) << std::endl;
        for (size_t i = 0; i < TWR_SIZE; i++)
        {
            std::cout << "list " << std::setw(3) << i << " head: ";
            lattice *temp = tw_1st + i;
            auto head = temp;
            std::cout << (void *)head;
            while (temp->next != head)
            {
                temp = temp->next;
                std::cout << " -> " << (void *)temp;
            }
            std::cout << "\n";
        }
        std::cout << "+++++++++++++++++++++++\n";
        for (size_t j = 0; j < 4; j++)
        {
            for (size_t i = 0; i < TWN_SIZE; i++)
            {
                std::cout << "list " << std::setw(3) << i << " head: ";
                lattice *temp = tw_nth[j] + i;
                auto head = temp;
                std::cout << (void *)head;
                while (temp->next != head)
                {
                    temp = temp->next;
                    std::cout << " -> " << (void *)temp;
                }
                std::cout << "\n";
            }
            std::cout << "+++++++++++++++++++++++\n";
        }
    }

private:
    lattice *calculate_lattice(uint32_t ticks)
    {
        uint32_t expired_tick = currtick.load(std::memory_order_acquire) + ticks;
        lattice *head{};
        if (ticks < TWR_SIZE)
        {
            head = tw_1st + FST_IDX(expired_tick);
        }
        else
        {
            for (size_t i = 0; i < 4; i++)
            {
                uint64_t sz = 1ull << (TWR_BITS + (i + 1) * TWN_BITS);
                if (ticks < sz)
                {
                    head = tw_nth[i] + NTH_IDX(expired_tick, i);
                    break;
                }
            }
        }
        return head;
    }

    void move_lattice_cascade(lattice *head)
    {
        while (head != head->next)
        {
            lattice *temp = head->next;
            temp->next->prev = temp->prev;
            temp->prev->next = temp->next;
            auto *head = calculate_lattice(temp->expired - currtick.load(std::memory_order_acquire));
            temp->prev = head->prev;
            temp->next = head;
            temp->prev->next = temp;
            head->prev = temp;
        }
    }

    void insert_lattice(uint32_t ticks, lattice *node)
    {
        std::unique_lock<std::mutex> lck(tw_mtx);
        auto *head = calculate_lattice(ticks);
        node->prev = head->prev;
        node->next = head;
        node->prev->next = node;
        head->prev = node;
    }

private:
    tw_fst_t tw_1st;
    tw_nth_t tw_nth[4];
    std::atomic_uint32_t currtick;
    std::mutex tw_mtx;
    Worker workers[1];
};

thread_local typename TimingWheel::lattice *TimingWheel::lattice::freelist = nullptr;

#endif