#include <stdexec/execution.hpp>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

template<typename Receiver>
struct State
{
    using operation_state_concept = stdexec::operation_state_tag;

    void start() noexcept
    {
        stdexec::set_value(std::move(_receiver));
    }

    Receiver _receiver;
};

struct Sender
{
    using sender_concept = stdexec::sender_tag;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t ()>;

    template<typename Receiver>
    auto connect(Receiver&& receiver)
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return State<Receiver_>{std::forward<Receiver>(receiver)};
    }
};

Sender work()
{
    return Sender{};
}

stdexec::task<void> work_coro()
{
    co_await work();
}

int main()
{
    std::optional<std::tuple<>> x1 = stdexec::sync_wait(work());
    assert(x1.has_value());
    std::optional<std::tuple<>> x2 = stdexec::sync_wait(work_coro());
    assert(x2.has_value());
}
