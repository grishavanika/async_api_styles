#include <print>
#include <coroutine>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

struct Co_Task
{
    struct promise_type;
    using co_handle = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        Co_Task get_return_object()
        {
            return Co_Task{co_handle::from_promise(*this)};
        }

        std::suspend_always initial_suspend()
        {
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        void return_void()
        {
            // yeah, we return void. Nothing to do
        }
       
        void unhandled_exception()
        {
            // crash, no exceptions handling
            assert(false);
        }
    };

    Co_Task(co_handle coro)
        : _coro{coro} {}
    Co_Task(Co_Task&& rhs) noexcept
        : _coro{std::exchange(rhs._coro, {})} { }
    Co_Task(const Co_Task&) = delete;
    ~Co_Task() noexcept
    {
        if (_coro)
        {
            _coro.destroy();
        }
    }

    void resume()
    {
        assert(_coro);
        assert(!_coro.done());
        _coro.resume();
    }

    co_handle _coro;
};

struct Co_CurlAsync
{
    bool await_ready()
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coro)
    {
        std::println("-- inside suspend, resuming immediately");
        coro.resume();
    }

    void await_resume()
    {
        std::println("-- resume");
    }
};

static Co_Task coro_work()
{
    std::println("before co_await");
    co_await Co_CurlAsync{};
    std::println("after co_await");
    co_return;
}

int main()
{
    Co_Task coro = coro_work();
    coro.resume();
}
