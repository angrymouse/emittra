#pragma once
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <any>
#include <future>
#include <chrono>
#include <stdexcept>
#include <memory>
#include <shared_mutex>
#include <condition_variable>
#include "concurrentqueue.h"

namespace emittra {

class Emittra {
public:
    using RespondFunction = std::function<void(const std::any&)>;
    using EventCallback = std::function<void(const RespondFunction&, const std::vector<std::any>&)>;

    Emittra(size_t initialQueueSize = 1024);

    void on(const std::string& event_name, EventCallback callback);
    void emit(const std::string& event_name, const std::vector<std::any>& args = {});
    void emit_with_token(const std::string& event_name, moodycamel::ProducerToken& token, const std::vector<std::any>& args = {});
    std::future<std::any> request(const std::string& event_name, const std::vector<std::any>& args = {});
    std::any blockingRequest(const std::string& event_name, const std::vector<std::any>& args = {}, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    void remove_listener(const std::string& event_name, const EventCallback& callback);
    void remove_all_listeners(const std::string& event_name);
    size_t listener_count(const std::string& event_name) const;
    void flush(const std::vector<std::string>& event_names = {});
    std::shared_ptr<std::condition_variable_any> make_cv(const std::vector<std::string>& event_names);
    void wait_for_event(const std::shared_ptr<std::condition_variable_any>& cv, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    bool try_dequeue(const std::string& event_name, std::vector<std::any>& args);
    bool try_dequeue_with_token(const std::string& event_name, moodycamel::ConsumerToken& token, std::vector<std::any>& args);
    bool enqueue_bulk(const std::string& event_name, const std::vector<std::vector<std::any>>& bulk_args);
    size_t try_dequeue_bulk(const std::string& event_name, std::vector<std::vector<std::any>>& bulk_args, size_t max_items);

private:
    struct QueuedEvent {
        std::vector<std::any> args;
        std::shared_ptr<std::promise<std::any>> response_promise;
    };

    struct EventData {
        std::vector<EventCallback> listeners;
        moodycamel::ConcurrentQueue<QueuedEvent> event_queue;
        mutable std::shared_mutex mutex;
        std::condition_variable_any cv;
        std::vector<std::weak_ptr<std::condition_variable_any>> associated_cvs;

        EventData(size_t initialSize);
    };

    mutable std::shared_mutex events_mutex;
    std::unordered_map<std::string, std::shared_ptr<EventData>> events;
    size_t initial_queue_size;

    std::shared_ptr<EventData> get_or_create_event_data(const std::string& event_name);
    void process_queue(const std::shared_ptr<EventData>& event_data);
    void notify_associated_cvs(const std::shared_ptr<EventData>& event_data);
};

namespace detail {
    template<typename... Args, std::size_t... I>
    std::tuple<Args...> demarshall_args_helper(const std::vector<std::any>& args, std::index_sequence<I...>) {
        return std::make_tuple(std::any_cast<Args>(args[I])...);
    }
}

template<typename... Args>
std::tuple<Args...> demarshall_args(const std::vector<std::any>& args) {
    if (sizeof...(Args) > args.size()) {
        throw std::runtime_error("Too few arguments provided");
    }
    return detail::demarshall_args_helper<Args...>(args, std::index_sequence_for<Args...>{});
}

} // namespace emittra
