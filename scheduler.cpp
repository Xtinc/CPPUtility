#include "scheduler.h"

thread_local Scheduler::lattice *Scheduler::lattice::freelist = nullptr;

Scheduler::Scheduler(uint32_t current_time)
    : tw_1st(std::make_unique<lattice[]>(TWR_SIZE)),
      tw_nth{std::make_unique<lattice[]>(TWN_SIZE),
             std::make_unique<lattice[]>(TWN_SIZE),
             std::make_unique<lattice[]>(TWN_SIZE),
             std::make_unique<lattice[]>(TWN_SIZE)},
      currtick(current_time), workers(std::make_unique<Worker>(*this))
{
    auto temp = tw_1st.get();
    for (size_t i = 0; i < TWR_SIZE; i++)
    {
        lattice::set_init(temp + i);
    }
    for (size_t j = 0; j < 4; j++)
    {
        temp = tw_nth[j].get();
        for (size_t i = 0; i < TWN_SIZE; i++)
        {
            lattice::set_init(temp + i);
        }
    }
}

void Scheduler::go()
{
    std::unique_lock<std::mutex> lck(tw_mtx);
    auto current_ticks = currtick.fetch_add(1, std::memory_order_release) + 1;
    auto index = FST_IDX(current_ticks);
    if (index == 0)
    {
        uint32_t i = 0;
        uint32_t tpx = 0;
        do
        {
            tpx = NTH_IDX(currtick, i);
            move_lattice_cascade(tw_nth[i].get() + tpx, current_ticks);

        } while (tpx == 0 && ++i < 4);
    }
    lattice *head = tw_1st.get() + index;
    while (head != head->next)
    {
        auto temp = head->next;
        workers->submit(std::move(temp->task));
        temp->next->prev = temp->prev;
        temp->prev->next = temp->next;
        delete temp;
    }
}

Scheduler::lattice *Scheduler::calculate_lattice(uint32_t ticks, uint32_t current_ticks)
{
    auto expired_tick = current_ticks + ticks;
    lattice *head{};
    if (ticks < TWR_SIZE)
    {
        head = tw_1st.get() + FST_IDX(expired_tick);
    }
    else
    {
        for (size_t i = 0; i < 4; i++)
        {
            uint64_t sz = 1ull << (TWR_BITS + (i + 1) * TWN_BITS);
            if (ticks < sz)
            {
                head = tw_nth[i].get() + NTH_IDX(expired_tick, i);
                break;
            }
        }
    }
    return head;
}

void Scheduler::move_lattice_cascade(lattice *head, uint32_t current_ticks)
{
    while (head != head->next)
    {
        lattice *temp = head->next;
        temp->next->prev = temp->prev;
        temp->prev->next = temp->next;
        auto pos = calculate_lattice(temp->task.expired - current_ticks, current_ticks);
        temp->prev = pos->prev;
        temp->next = pos;
        temp->prev->next = temp;
        head->prev = temp;
    }
}

void Scheduler::insert_lattice(uint32_t ticks, lattice *node)
{
    std::unique_lock<std::mutex> lck(tw_mtx);
    auto current_ticks = currtick.load(std::memory_order_acquire);
    node->task.expired = current_ticks + ticks;
    auto head = calculate_lattice(ticks, current_ticks);
    node->prev = head->prev;
    node->next = head;
    node->prev->next = node;
    head->prev = node;
}

Worker::Worker(Scheduler &_tw)
    : queue(std::make_unique<TaskObj[]>(MAX_SIZE)),
      front(1), rear(0), stop(false), tw(_tw)
{
    for (size_t i = 0; i < 2; i++)
    {
        thd.emplace_back(&Worker::do_work, this);
    }
}

Worker::~Worker()
{
    {
        std::unique_lock<std::mutex> lck(mtx);
        stop = true;
    }
    cond.notify_all();
    for (auto &ele : thd)
    {
        ele.join();
    }
}

bool Worker::submit(TaskObj &&obj)
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

void Worker::do_work()
{
    for (;;)
    {
        TaskObj task;
        {
            std::unique_lock<std::mutex> lck(mtx);
            cond.wait(lck, [this]()
                      { return stop || (length() != 0); });
            if (stop && length() == 0)
            {
                return;
            }
            task = std::move(queue[front]);
            front = (front + 1) % MAX_SIZE;
        }
        task.started = tw.now();
        try
        {
            task.func();
        }
        catch (const std::exception &e)
        {
            printf("error: %s\n", e.what());
            throw e;
        }
        auto exceed_ticks = tw.now() - task.started;
        auto penalty_ticks = 0;
        if (exceed_ticks > task.duration)
        {
            penalty_ticks = exceed_ticks;
            printf("task time out!: %lu\n", exceed_ticks);
        }
        if (--task.counters != 0)
        {
            tw.insert_lattice(task.duration + penalty_ticks,
                              new Scheduler::lattice{nullptr, nullptr, std::move(task)});
        }
    }
}