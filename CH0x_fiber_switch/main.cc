#include <Windows.h>
// Note: the floating-point state on x86 systems is not preserved.
// If there is need to support x86, Fiber's Ex-tended API must be used.
#if !defined(_WIN64)
#  error Fiber implementation does not support x86 systems.
#endif
#include <type_traits>
static_assert(std::is_same_v<LPVOID, void*>);

#include <print>
#include <functional>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

struct Fiber
{
    void* _fiber = nullptr;
    void* _parent_fiber = nullptr;
    // For an example:
    std::function<void ()> _callback;

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
    }

    void resume()
    {
        assert(_parent_fiber == nullptr);
        _parent_fiber = ::GetCurrentFiber();
        ::SwitchToFiber(_fiber);
    }

    void run();

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

void Fiber::run()
{
    assert(_callback);
    _callback();
}

int main()
{
    Fiber::Boot _;

    Fiber fiber1;
    Fiber fiber2;
    fiber1._callback = [&fiber2, self = &fiber1]()
    {
        std::println("fiber1: start");
        fiber2.resume(); // switch to fiber2
        std::println("fiber1: END");
        self->suspend(); // switch to main (=our parent)
    };
    fiber2._callback = [self = &fiber2]()
    {
        std::println("fiber2: start");
        self->suspend(); // switch back to fiber1 (=our parent)
        std::println("fiber2: END");
        self->suspend(); // switch to main (=our parent)
    };
    std::println("main1");
    fiber1.resume();
    std::println("main2");
    fiber2.resume();
    std::println("main3");
}
