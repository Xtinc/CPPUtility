#ifndef USER_SCHEDULER_HEADER
#define USER_SCHEDULER_HEADER

#include <type_traits>
#include <functional>
#include <thread>
#include <future>
#include <mutex>
#include <vector>
#include <atomic>
// only for debug
#include <iostream>
#include <iomanip>

#define SCHD_ASYNC_TASK ((void *)nullptr)

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

struct TaskObj
{
    uint32_t started{};
    uint32_t expired{};
    uint32_t duration{};
    uint32_t counters{};
    std::function<void()> func{};
    std::function<void(uint32_t exceed_tick, uint32_t &counters)> hand;
};

class Worker;

class Scheduler
{
    friend class Worker;
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
        lattice *prev{};
        lattice *next{};
        TaskObj task{};

        static void set_init(lattice *node)
        {
            node->prev = node;
            node->next = node;
        }

        static void free()
        {
            std::lock_guard<std::mutex> grd(mem_mtx);
            while (freelist != nullptr)
            {
                auto temp = freelist;
                freelist = freelist->next;
                ::delete temp;
            }
        }

        static void print_free_list()
        {
            // for debug
            auto head = freelist;
            std::cout << "freelist: " << (void *)head;
            while (head != nullptr)
            {
                head = head->next;
                std::cout << " -> " << (void *)head;
            }
            std::cout << "\n";
        }

        void *operator new(size_t)
        {
            std::lock_guard<std::mutex> grd(mem_mtx);
            if (freelist == nullptr)
            {
                return ::new lattice;
            }
            auto *temp = freelist;
            freelist = freelist->next;
            return temp;
        }

        void operator delete(void *ptr)
        {
            std::lock_guard<std::mutex> grd(mem_mtx);
            ((lattice *)ptr)->next = freelist;
            freelist = (lattice *)ptr;
        }

    private:
        static lattice *freelist;
        static std::mutex mem_mtx;
    };

    using tw_fst_t = lattice *[TWR_SIZE];
    using tw_nth_t = lattice *[TWN_SIZE];

public:
    explicit Scheduler(uint32_t current_time = 0);
    ~Scheduler();

    void go();

    uint32_t now() const
    {
        return currtick.load(std::memory_order_acquire);
    }

    void set_task(RelativeTimeTick time, TaskObj obj)
    {
        insert_lattice(time.tick, new lattice{nullptr, nullptr, std::move(obj)});
    }

    void set_task(RelativeTimeTick time, TaskObj obj)
    {
        insert_lattice(time.tick, new lattice{nullptr, nullptr, std::move(obj)}, 'a');
    }

    template <class Fn, class... Args>
    void set_task(RelativeTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, {0, 0, 0xFFFFFFFF, 1, std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)}};
        insert_lattice(time.tick, temp);
    }

    template <class Fn, class... Args>
    void set_task(AbsoluteTimeTick time, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, {0, 0, 0xFFFFFFFF, 1, std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)}};
        insert_lattice(time.tick, temp, 'a');
    }

    template <class Fn, class... Args>
    void set_task(RelativeTimeTick time, AbsoluteTimeTick period, uint32_t cycles, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, {0, 0, period.tick, cycles, std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)}};
        insert_lattice(time.tick, temp);
    }

    template <class Fn, class... Args>
    void set_task(AbsoluteTimeTick time, AbsoluteTimeTick period, uint32_t cycles, Fn &&Fx, Args &&...Ax)
    {
        auto temp = new lattice{nullptr, nullptr, {0, 0, period.tick, cycles, std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...)}};
        insert_lattice(time.tick, temp, 'c');
    }

    template <class Fn, class... Args>
    auto set_task(RelativeTimeTick time, void *, Fn &&Fx, Args &&...Ax)
        -> std::future<typename std::result_of<Fn(Args...)>::type>
    {
        using rt = typename std::result_of<Fn(Args...)>::type;
        auto task_ptr =
            std::make_shared<std::packaged_task<rt()>>(std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...));
        auto temp = new lattice{nullptr, nullptr, {0, 0, 0xFFFFFFFF, 1, [task_ptr]()
                                                   { (*task_ptr)(); }}};
        insert_lattice(time.tick, temp);
        return task_ptr->get_future();
    }

    template <class Fn, class... Args>
    auto set_task(AbsoluteTimeTick time, void *, Fn &&Fx, Args &&...Ax)
        -> std::future<typename std::result_of<Fn(Args...)>::type>
    {
        using rt = typename std::result_of<Fn(Args...)>::type;
        auto task_ptr =
            std::make_shared<std::packaged_task<rt()>>(std::bind(std::forward<Fn>(Fx), std::forward<Args>(Ax)...));
        auto temp = new lattice{nullptr, nullptr, {0, 0, 0xFFFFFFFF, 1, [task_ptr]()
                                                   { (*task_ptr)(); }}};
        insert_lattice(time.tick, temp, 'a');
        return task_ptr->get_future();
    }

    // only for debug
    void print_self()
    {
        std::cout << "sizeof lattice: " << sizeof(lattice) << std::endl;
        for (size_t i = 0; i < TWR_SIZE; i++)
        {
            std::cout << "list " << std::setw(3) << i << " head: ";
            lattice *temp = tw_1st[i];
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
                lattice *temp = tw_nth[j][i];
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
    lattice *calculate_lattice(uint32_t ticks, uint32_t current_ticks);

    void move_lattice_cascade(lattice *head, uint32_t current_ticks);

    void insert_lattice(uint32_t ticks, lattice *node, char isRelative = 'r');

private:
    tw_fst_t tw_1st;
    tw_nth_t tw_nth[4];
    std::atomic_uint32_t currtick;
    std::mutex tw_mtx;
    std::unique_ptr<Worker> workers;
};

class Worker
{
    constexpr static auto MAX_SIZE = 101;

public:
    explicit Worker(Scheduler &_tw);

    ~Worker();

    bool submit(TaskObj &&obj);

private:
    void do_work();

    int length() const
    {
        return ((rear + MAX_SIZE) - front + 1) % MAX_SIZE;
    }

private:
    std::unique_ptr<TaskObj[]> queue;
    int front;
    int rear;
    bool stop;
    Scheduler &tw;
    std::vector<std::thread> thd;
    std::mutex mtx;
    std::condition_variable cond;
};
#endif