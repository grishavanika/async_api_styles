#include <utility>
#include <variant>
#include <future>
#include <type_traits>
#include <print>

#include <cassert>

#define FWD(...) ::std::forward<decltype(__VA_ARGS__)>(__VA_ARGS__)
#define MOV(...) ::std::move(__VA_ARGS__)
#define REMOVE_CV(...) std::remove_cv_t<__VA_ARGS__>
using void_t = std::monostate;

// Receiver.
template<typename Receiver, typename T>
void set_value(Receiver&& r, T&& v) noexcept
{
    FWD(r).set_value(FWD(v));
}

template<typename Receiver, typename E>
void set_error(Receiver&& r, E&& e) noexcept
{
    FWD(r).set_error(FWD(e));
}

template<typename Receiver>
void set_stopped(Receiver&& r) noexcept
{
    FWD(r).set_stopped();
}

// Sender.
template<typename Sender, typename Receiver>
auto connect(Sender&& s, Receiver&& r) noexcept
{
    return FWD(s).connect(FWD(r));
}

template<typename Sender>
using sender_value_t = typename REMOVE_CV(Sender)::value_t;

template<typename Sender>
using sender_error_t = typename REMOVE_CV(Sender)::error_t;

// Operation.
template<typename Operation>
void start(Operation& o) noexcept
{
    o.start();
}

// just(1)
template<typename Receiver, typename T>
struct State_Just
{
    Receiver _r;
    T _v;
    void start() noexcept
    {
        ::set_value(MOV(_r), MOV(_v));
    }
};

template<typename T>
struct Sender_Just
{
    using value_t = T;
    using error_t = void_t;
    T _v;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Just<REMOVE_CV(Receiver), T>{._r = FWD(r), ._v = MOV(_v)};
    }
};

template<typename T>
auto just(T&& v) noexcept
{
    return Sender_Just<REMOVE_CV(T)>{._v = FWD(v)};
}

// sync_wait(just(1))
template<typename T, typename E>
struct sync_wait_result : std::variant<std::monostate, T, E>
{
    bool was_stopped() const { return (this->index() == 0); }
    bool has_value() const { return (this->index() == 1); }
    bool has_error() const { return (this->index() == 2); }
    template<typename U>
    void set_value(U&& v) { this->template emplace<1>(FWD(v)); }
    template<typename U>
    void set_error(U&& e) { this->template emplace<2>(FWD(e)); }
    void set_stopped() { this->template emplace<0>(); }
    T& value() { return std::get<1>(*this); }
    E& error() { return std::get<2>(*this); }
};

template<typename T, typename E>
struct State_SyncWait : sync_wait_result<T, E>
{
    std::promise<void> _done;
    auto get_once()
    {
        return sync_wait_result<T, E>{std::move(*this)};
    }
    void wait()
    {
        _done.get_future().wait();
    }
};

template<typename T, typename E>
struct Receiver_SyncWait
{
    State_SyncWait<T, E>* _state = nullptr;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        _state->set_value(FWD(v));
        _state->_done.set_value();
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        _state->set_error(FWD(e));
        _state->_done.set_value();
    }
    void set_stopped() noexcept
    {
        _state->set_stopped();
        _state->_done.set_value();
    }
};

template<typename Sender>
auto sync_wait(Sender&& s) noexcept
{
    using T = sender_value_t<Sender>;
    using E = sender_error_t<Sender>;
    State_SyncWait<T, E> state;
    auto o = connect(FWD(s), Receiver_SyncWait<T, E>{._state = &state});
    start(o);
    state.wait();
    return state.get_once();
}

// then(just(1), [](int v) { return ...; })
template<typename Receiver, typename Lambda>
struct Receiver_Then
{
    Receiver _r;
    Lambda _f;
    template<typename U>
    void set_value(U&& v) noexcept
    {
        ::set_value(MOV(_r), MOV(_f)(FWD(v)));
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        ::set_error(MOV(_r), FWD(e));
    }
    void set_stopped() noexcept
    {
        ::set_stopped(MOV(_r));
    }
};

template<typename Sender, typename Lambda>
struct Sender_Then
{
    using inner_value_t = sender_value_t<Sender>;
    using value_t = std::invoke_result_t<Lambda, inner_value_t>;
    using error_t = sender_error_t<Sender>;
    Sender _s;
    Lambda _f;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        using Receiver_ = Receiver_Then<REMOVE_CV(Receiver), Lambda>;
        return ::connect(MOV(_s), Receiver_{._r = FWD(r), ._f = MOV(_f)});
    }
};

template<typename Sender, typename Lambda>
auto then(Sender&& s, Lambda&& f) noexcept
{
    return Sender_Then<REMOVE_CV(Sender), REMOVE_CV(Lambda)>{._s = FWD(s), ._f = FWD(f)};
}

// async()
template<typename Receiver>
struct State_Async
{
    Receiver _r;
    std::future<void> _f;
    void start() noexcept
    {
        _f = std::async(std::launch::async
            , [r = MOV(_r)]() mutable
        {
            ::set_value(MOV(r), void_t());
        });
    }
};

struct Sender_Async
{
    using value_t = void_t;
    using error_t = void_t;
    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        return State_Async<REMOVE_CV(Receiver)>{._r = FWD(r)};
    }
};

auto async()
{
    return Sender_Async{};
}

// when_all()
template<typename Receiver, typename Is, typename... Senders>
struct Result_When_All;

template<typename Receiver, auto... Is, typename... Senders>
struct Result_When_All<Receiver, std::index_sequence<Is...>, Senders...>
{
    using Results = std::tuple<
        sync_wait_result<
              sender_value_t<Senders>
            , sender_error_t<Senders>
            >...
        >;
    Receiver _r;
    Results _rs;
    bool _has_error = false;
    bool _was_stopped = false;
    std::int32_t _count = sizeof...(Senders);

    template<auto I, typename U>
    void set_value(U&& v) noexcept
    {
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_value(FWD(v));
        }
        try_finish();
    }
    template<auto I, typename U>
    void set_error(U&& e) noexcept
    {
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_error(FWD(e));
            _has_error = true;
            // real when_all() - also cancels the rest
        }
        try_finish();
    }
    template<auto I>
    void set_stopped() noexcept
    {
        if ((_has_error || _was_stopped) == false)
        {
            std::get<I>(_rs).set_stopped();
            _was_stopped = true;
            // real when_all() - also cancels the rest
        }
        try_finish();
    }

    void try_finish()
    {
        _count -= 1;
        assert(_count >= 0);
        if (_count == 0)
        {
            finish();
        }
    }

    void finish()
    {
        if (_was_stopped)
        {
            ::set_stopped(MOV(_r));
        }
        else if (_has_error)
        {
            std::variant<sender_error_t<Senders>...> es;
            ((std::get<Is>(_rs).has_error()
                ? (void)es.template emplace<Is>(
                    MOV(std::get<Is>(_rs).error()))
                : (void)0
                ), ...);
            ::set_error(MOV(_r), MOV(es));
            return;
        }
        else
        {
            ::set_value(MOV(_r)
                , std::tuple<sender_value_t<Senders>...>(
                    std::get<Is>(_rs).value()...
                    )
                );
        }
    }
};

template<typename Sender, auto I, typename Results>
struct Receiver_When_All
{
    Results* _r = nullptr;

    template<typename U>
    void set_value(U&& v) noexcept
    {
        _r->template set_value<I>(FWD(v));
    }
    template<typename U>
    void set_error(U&& e) noexcept
    {
        _r->template set_error<I>(FWD(e));
    }
    void set_stopped() noexcept
    {
        _r->template set_stopped<I>();
    }
};

template<typename Sender, typename Receiver>
using operation_state_t = decltype(::connect(
    std::declval<Sender>(), std::declval<Receiver>()));

template<typename Sender, typename Receiver>
using state_storage_t = std::variant<std::monostate
    , operation_state_t<Sender, Receiver>>;

template<typename Receiver, typename SendersTuple, typename SendersIndexes>
struct State_When_All;

template<typename Receiver, typename... Senders, auto... Is>
struct State_When_All<Receiver
    , std::tuple<Senders...>     // Senders tuple
    , std::index_sequence<Is...> // Senders indexes
    >
{
    using Results = Result_When_All<
          Receiver
        , std::index_sequence<Is...>
        , Senders...
        >;
    using operations_tuple_t = std::tuple<
        state_storage_t<Senders
            , Receiver_When_All<Senders, Is, Results>
            >...
        >;
    using senders_tuple = std::tuple<Senders...>;

    Results _results;
    senders_tuple _ss;
    operations_tuple_t _states;

    template<auto I>
    void apply_sender()
    {
        using Sender_ = std::tuple_element_t<I, senders_tuple>;
        using Receiver_ = Receiver_When_All<Sender_, I, Results>;

        auto& sender = std::get<I>(_ss);
        auto& state_storage = std::get<I>(_states);
        auto& state = state_storage.template emplace<1>(
            ::connect(MOV(sender), Receiver_{._r = &_results}));
        ::start(state);
    }

    void start() noexcept
    {
        (apply_sender<Is>(), ...);
    }
};

template<typename... Senders>
struct Sender_When_All
{
    using value_t = std::tuple<sender_value_t<Senders>...>;
    using error_t = std::variant<sender_error_t<Senders>...>;

    std::tuple<Senders...> _ss;

    template<typename... Ss>
    Sender_When_All(Ss&&... ss)
        : _ss{FWD(ss)...}
    {
    }

    template<typename Receiver>
    auto connect(Receiver&& r) noexcept
    {
        using State = State_When_All<
              REMOVE_CV(Receiver)
            , std::tuple<Senders...>
            , std::index_sequence_for<Senders...>
            >;
        return State{._results{._r = FWD(r)}, ._ss{MOV(_ss)}};
    }
};

template<typename... Senders>
auto when_all(Senders&&... ss)
{
    static_assert(sizeof...(Senders) >= 1);
    return Sender_When_All<REMOVE_CV(Senders)...>{FWD(ss)...};
}

int main()
{
    auto async_just = [](auto v)
    {
        return then(async(), [copy = MOV(v)](void_t) mutable
        {
            return MOV(copy);
        });
    };
    auto op = when_all(
          async_just(6)
        , async_just('v')
        , just(7.2)
        );
    sync_wait(then(MOV(op), [](auto vs) -> void_t
    {
        auto [a, b, c] = vs;
        std::println("{} {} {}", a, b, c);
        return {};
    }));
}
