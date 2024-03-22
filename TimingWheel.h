#ifndef TIMING_WHEEL_HEADER
#define TIMING_WHEEL_HEADER

#include <cstddef>
#include <iostream>
// only for debug
#include <iomanip>

template <typename F>
class TimingWheel
{
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
        F ele;

        static void set_init(lattice *node)
        {
            node->prev = node;
            node->next = node;
            node->expired = 0;
        }

        static void swap_head(lattice *head1, lattice *head2)
        {
            if (head1 != head1->next)
            {
                auto *head = head1->next;
                auto *tail = head1->prev;
                auto *at = head2->next;
                head->prev = head2;
                head2->next = head;
                tail->next = at;
                at->prev = last;
                set_init(head1);
            }
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
    TimingWheel(uint64_t current_time)
        : lasttime(current_time), currtick(0)
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

    void set_task(const F &f, uint32_t ticks)
    {
        auto *head = calculate_lattice(ticks);
        auto *temp = new lattice{head->prev, head, ticks, f};
        temp->prev->next = temp;
        head->prev = temp;
    }

    void go()
    {
        currtick++;
        auto index = FST_IDX(currtick);
        if (index == 0)
        {
            uint32_t i = 0;
            uint32_t tpx = 0;
            do
            {
                tpx = NTH_IDX(currtick, i);
                move_lattice_cascade(tw_nth[i] + tpx);

            } while (tpx == 0 && ++i < 4);
        }
        lattice *head = tw_1st + index;
        while (head != head->next)
        {
            lattice *temp = head->next;
            std::cout << " ptr: " << (void *)temp << " ele: " << temp->ele << " ticks: " << temp->expired << std::endl;
            temp->next->prev = temp->prev;
            temp->prev->next = temp->next;
            delete temp;
        }
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
        uint32_t expired_tick = currtick + (ticks > 0 ? ticks : 1);
        uint32_t idx = expired_tick - currtick;
        lattice *head{};
        if (idx < TWR_SIZE)
        {
            head = tw_1st + FST_IDX(expired_tick);
        }
        else
        {
            for (size_t i = 0; i < 4; i++)
            {
                uint64_t sz = 1ull << (TWR_BITS + (i + 1) * TWN_BITS);
                if (idx < sz)
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
            auto *head = calculate_lattice(temp->expired);
            temp->prev = head->prev;
            temp->next = head;
            temp->prev->next = temp;
            head->prev = temp;
        }
    }

private:
    tw_fst_t tw_1st;
    tw_nth_t tw_nth[4];
    uint64_t lasttime;
    uint32_t currtick;
};

template <typename F>
thread_local typename TimingWheel<F>::lattice *TimingWheel<F>::lattice::freelist = nullptr;

#endif