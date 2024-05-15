#include "scheduler.h"

Scheduler::lattice *Scheduler::lattice::freelist = nullptr;
std::mutex Scheduler::lattice::mem_mtx = {};

Scheduler::Scheduler(uint32_t current_time)
    : currtick(current_time), workers(std::make_unique<Worker>(*this))
{
    for (size_t i = 0; i < TWR_SIZE; i++)
    {
        tw_1st[i] = new lattice;
        lattice::set_init(tw_1st[i]);
    }
    for (size_t j = 0; j < 4; j++)
    {
        auto &headn = tw_nth[j];
        for (size_t i = 0; i < TWN_SIZE; i++)
        {
            headn[i] = new lattice;
            lattice::set_init(headn[i]);
        }
    }
}

Scheduler::~Scheduler()
{
    for (size_t i = 0; i < TWR_SIZE; i++)
    {
        lattice *head = tw_1st[i];
        while (head != head->next)
        {
            auto temp = head->next;
            temp->next->prev = temp->prev;
            temp->prev->next = temp->next;
            temp->task = {};
            delete temp;
        }
        delete head;
    }
    for (size_t j = 0; j < 4; j++)
    {
        auto &headn = tw_nth[j];
        for (size_t i = 0; i < TWN_SIZE; i++)
        {
            lattice *head = headn[i];
            while (head != head->next)
            {
                auto temp = head->next;
                temp->next->prev = temp->prev;
                temp->prev->next = temp->next;
                temp->task = {};
                delete temp;
            }
            delete head;
        }
    }
    lattice::free();
}

void Scheduler::go()
{
    std::lock_guard<std::mutex> grd(tw_mtx);
    auto current_ticks = currtick.fetch_add(1, std::memory_order_release);
    auto index = FST_IDX(current_ticks);
    if (index == 0)
    {
        uint32_t i = 0;
        uint32_t tpx = 0;
        do
        {
            tpx = NTH_IDX(currtick, i);
            move_lattice_cascade(tw_nth[i][tpx], current_ticks);

        } while (tpx == 0 && ++i < 4);
    }
    lattice *head = tw_1st[index];
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
        head = tw_1st[FST_IDX(expired_tick)];
    }
    else
    {
        for (size_t i = 0; i < 4; i++)
        {
            uint64_t sz = 1ull << (TWR_BITS + (i + 1) * TWN_BITS);
            if (ticks < sz)
            {
                head = tw_nth[i][NTH_IDX(expired_tick, i)];
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

void Scheduler::insert_lattice(uint32_t ticks, lattice *node, char isRelative)
{
    std::lock_guard<std::mutex> grd(tw_mtx);
    auto current_ticks = currtick.load(std::memory_order_acquire);
    if (isRelative == 'a' && ticks < current_ticks)
    {
        return;
    }
    uint32_t relative_ticks;
    switch (isRelative)
    {
    case 'r':
        relative_ticks = ticks;
        break;
    case 'a':
        relative_ticks = ticks - current_ticks;
        break;
    case 'c':
        relative_ticks = (currtick / ticks + 1) * ticks - currtick + 1;
        break;
    default:
        return;
    }
    node->task.expired = current_ticks + relative_ticks;
    auto head = calculate_lattice(relative_ticks, current_ticks);
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
        std::lock_guard<std::mutex> grd(mtx);
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
        std::lock_guard<std::mutex> grd(mtx);
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
        auto penalty_ticks = task.duration != 0 ? -1 : 0;
        if (task.duration != 0 && exceed_ticks > task.duration)
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