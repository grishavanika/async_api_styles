#include <Windows.h>
// Note: the floating-point state on x86 systems is not preserved.
// If there is need to support x86, Fiber's Ex-tended API must be used.
#if !defined(_WIN64)
#  error Fiber implementation does not support x86 systems.
#endif
#include <type_traits>
static_assert(std::is_same_v<LPVOID, void*>);

#include <print>
#include <exception>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

// To separate `FiberTask` implementation from an actual `Fiber`.
struct FiberCallbackBase
{
    void run()    { return do_run();    }
    void resume() { return do_resume(); }

    FiberCallbackBase() noexcept = default;
    FiberCallbackBase(const FiberCallbackBase& rhs) = delete;
protected:
    ~FiberCallbackBase() noexcept = default;
    virtual void do_run() = 0;
    virtual void do_resume() {} // optional
};

struct Fiber
{
    void* _fiber = nullptr;
    void* _parent_fiber = nullptr;
    FiberCallbackBase* _callback = nullptr;
    std::exception_ptr _exception;

    Fiber(const Fiber&) = delete;
    Fiber()
    {
        _fiber = ::CreateFiber(
            0 // default stack size
            , &FiberProc
            , this); // parameter to FiberProc
        assert(_fiber);
    }
    ~Fiber()
    {
        ::DeleteFiber(_fiber);
        _fiber = nullptr;
    }

    static void FiberProc(void* parameter)
    {
        assert(parameter);
        Fiber& self = *static_cast<Fiber*>(parameter);
        while (true)
        {
            self.run();
            self.suspend();
        }
    }

    void suspend()
    {
        assert(_parent_fiber);
        void* switch_to_fiber = _parent_fiber;
        _parent_fiber = nullptr;
        ::SwitchToFiber(switch_to_fiber);
        assert(_callback);
        _callback->resume();
    }

    void resume()
    {
        assert(_parent_fiber == nullptr);
        assert(_callback);
        _parent_fiber = ::GetCurrentFiber();
        ::SwitchToFiber(_fiber);
    }

    void run()
    {
        try
        {
            _callback->resume();
            _callback->run();
        }
        catch (...)
        {
            _exception = std::current_exception();
        }
        _callback = nullptr;
    }

    void set_callback(FiberCallbackBase& callback)
    {
        assert(_callback == nullptr);
        _callback = &callback;
        _exception = {};
    }

    bool is_busy() const
    {
        if (_exception)
        {
            std::rethrow_exception(_exception);
        }
        return !!_callback;
    }

    struct Boot
    {
        Boot(const Boot&) = delete;
        Boot()
        {
            const void* fiber = ::ConvertThreadToFiber(nullptr);
            assert(fiber);
        }
        ~Boot()
        {
            const auto ok = ::ConvertFiberToThread();
            assert(ok);
        }
    };
};

namespace this_fiber
{
void suspend()
{
    assert(::IsThreadAFiber());
    void* fiber_data = ::GetFiberData();
    assert(fiber_data);
    Fiber& self = *static_cast<Fiber*>(fiber_data);
    self.suspend();
}
} // namespace this_fiber

struct MyFiberTask : FiberCallbackBase
{
    bool _cancel = false;

    virtual void do_run() override
    {
        std::println("fiber1");
        this_fiber::suspend();
        std::println("fiber2");
    }

    virtual void do_resume() override
    {
        if (_cancel)
        {
            _cancel = false;
            throw std::exception("canceled");
        }
    }
};

int main()
{
    Fiber::Boot _;

    Fiber fiber;
    MyFiberTask task;
    fiber.set_callback(task);
    std::println("main1");
    fiber.resume();
    std::println("main2");
    task._cancel = true;
    fiber.resume();
    std::println("main3");
}
