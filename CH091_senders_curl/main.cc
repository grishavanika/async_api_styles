#include <print>
#include <string>
#include <functional>
#include <unordered_map>

#include <curl/curl.h>

#include <stdexec/execution.hpp>
#include <exec/sequence.hpp>

#if defined(NDEBUG)
#  undef NDEBUG
#endif
#include <cassert>

// libcurl bookkeeping
using CURL_Async = void*;
CURL_Async CURL_async_create();
void CURL_async_destroy(CURL_Async curl_async);
void CURL_async_tick(CURL_Async curl_async);

// main async callback API
void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response));

static size_t CURL_OnWriteCallback(void* ptr, size_t size, size_t nmemb, void* data)
{
    std::string& response = *static_cast<std::string*>(data);
    response.append(static_cast<const char*>(ptr), size * nmemb);
    return (size * nmemb);
}

struct CURL_AsyncScheduler
{
    CURL_AsyncScheduler();
    ~CURL_AsyncScheduler();
    // no copy, no move
    CURL_AsyncScheduler(const CURL_AsyncScheduler&) = delete;

    using Callback = std::function<void (CURL* curl_easy)>;

    void tick();
    void add_request(CURL* curl_easy, Callback on_finish);

    // our state
    CURLM* _multi_curl = nullptr;
    std::unordered_map<CURL*, Callback> _curl_to_callback;
};

CURL_AsyncScheduler::CURL_AsyncScheduler()
{
    const CURLcode status = curl_global_init(CURL_GLOBAL_ALL);
    assert(status == CURLE_OK);
    _multi_curl = curl_multi_init();
    assert(_multi_curl);
}

CURL_AsyncScheduler::~CURL_AsyncScheduler()
{
    const CURLMcode status = curl_multi_cleanup(_multi_curl);
    assert(status == CURLM_OK);
    curl_global_cleanup();
}

void CURL_AsyncScheduler::tick()
{
    int running_handles = -1;
    CURLMcode status = curl_multi_perform(_multi_curl, &running_handles);
    assert(status == CURLM_OK);
    int msgs_in_queue = 0;
    while (CURLMsg* m = curl_multi_info_read(_multi_curl, &msgs_in_queue))
    {
        if (m->msg != CURLMSG_DONE)
        {
            continue;
        }
        CURL* curl_easy = m->easy_handle;
        assert(curl_easy);
        status = curl_multi_remove_handle(_multi_curl, curl_easy);
        assert(status == CURLM_OK);
        auto it = _curl_to_callback.find(curl_easy);
        assert(it != _curl_to_callback.end());
        Callback callback = std::move(it->second);
        assert(callback);
        (void)_curl_to_callback.erase(it);
        callback(curl_easy);
    }
}

void CURL_AsyncScheduler::add_request(CURL* curl_easy, Callback on_finish)
{
    assert(on_finish);
    assert(curl_easy);
    assert(!_curl_to_callback.contains(curl_easy));
    const CURLMcode status = curl_multi_add_handle(_multi_curl, curl_easy);
    assert(status == CURLM_OK);
    _curl_to_callback[curl_easy] = std::move(on_finish);
}

CURL_Async CURL_async_create()
{
    CURL_AsyncScheduler* scheduler = new(std::nothrow) CURL_AsyncScheduler();
    assert(scheduler);
    return scheduler;
}

void CURL_async_destroy(CURL_Async curl_async)
{
    assert(curl_async);
    CURL_AsyncScheduler* scheduler = static_cast<CURL_AsyncScheduler*>(curl_async);
    delete scheduler;
}

static CURL_AsyncScheduler& CURL_scheduler(CURL_Async curl_async)
{
    CURL_AsyncScheduler* scheduler = static_cast<CURL_AsyncScheduler*>(curl_async);
    assert(scheduler);
    return *scheduler;
}

void CURL_async_tick(CURL_Async curl_async)
{
    CURL_scheduler(curl_async).tick();
}

void CURL_async_get(CURL_Async curl_async
    , const std::string& url
    , void* user_data
    , void (*callback)(void* user_data, std::string response))
{
    // 1. setup curl easy handle
    CURL* curl_easy = curl_easy_init();
    assert(curl_easy);
    CURLcode status = curl_easy_setopt(curl_easy, CURLOPT_URL, url.c_str());
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_FOLLOWLOCATION, 1L);
    assert(status == CURLE_OK);
    
    // 2. write response data to separate std::string
    std::string* state = new(std::nothrow) std::string{};
    assert(state);
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEFUNCTION, CURL_OnWriteCallback);
    assert(status == CURLE_OK);
    status = curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, state);
    assert(status == CURLE_OK);

    // 3. associate with multi handle/event loop
    CURL_scheduler(curl_async).add_request(curl_easy
        , [state, user_data, callback](CURL* curl_easy_)
    {
        long response_code = -1;
        const CURLcode status_ = curl_easy_getinfo(curl_easy_, CURLINFO_RESPONSE_CODE, &response_code);
        assert(status_ == CURLE_OK);
        assert(response_code == 200L && "RUN serve.cmd");
        curl_easy_cleanup(curl_easy_);
        std::string data = std::move(*state);
        delete state;
        callback(user_data, std::move(data));
    });
}

template<typename Receiver>
struct CURL_Get_State
{
    using operation_state_concept = stdexec::operation_state_tag;

    void start() noexcept
    {
        assert(_curl_async);
        CURL_async_get(_curl_async, _url
            , this
            , [](void* user_data, std::string response)
        {
            CURL_Get_State& state = *static_cast<CURL_Get_State*>(user_data);
            stdexec::set_value(std::move(state._receiver), std::move(response));
        });
    }

    Receiver _receiver;
    CURL_Async _curl_async = nullptr;
    std::string _url;
};

struct CURL_Get_Sender
{
    using sender_concept = stdexec::sender_tag;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t (std::string)>;

    CURL_Async _curl_async = nullptr;
    std::string _url;

    template<typename Receiver>
    auto connect(Receiver&& receiver)
    {
        using Receiver_ = std::remove_cvref_t<Receiver>;
        return CURL_Get_State<Receiver_>
        {
            ._receiver = std::forward<Receiver>(receiver),
            ._curl_async = _curl_async,
            ._url = std::move(_url)
        };
    }
};

CURL_Get_Sender CURL_sender_get(CURL_Async curl_async, const std::string& url)
{
    return CURL_Get_Sender
    {
        ._curl_async = curl_async,
        ._url = url
    };
}

auto Senders_Main(CURL_Async curl_async)
{
    return exec::sequence(
        CURL_sender_get(curl_async, "localhost:5001/file1.txt")
            | stdexec::then([](std::string r1)
        {
            std::println("{}", r1);
        }),
        CURL_sender_get(curl_async, "localhost:5001/file2.txt")
            | stdexec::then([](std::string r2)
        {
            std::println("{}", r2);
        }));
}

struct AnyReceiver
{
    using receiver_concept = stdexec::receiver_tag;

    template<typename... Args>
    void set_value(Args&&...) noexcept { finish(); }
    template<typename... Args>
    void set_error(Args&&...) noexcept { finish(); }
    void set_stopped() noexcept        { finish(); }

    void finish() noexcept
    {
        assert(_done);
        assert(*_done == false);
        *_done = true;
    }

    bool* _done = nullptr;
};

int main()
{
    CURL_Async curl_async = CURL_async_create();
    bool done = false;
    auto state = stdexec::connect(Senders_Main(curl_async), AnyReceiver{&done});
    stdexec::start(state);
    while (done == false)
    {
        CURL_async_tick(curl_async);
    }
    CURL_async_destroy(curl_async);
}
