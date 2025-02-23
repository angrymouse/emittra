#include "emittra.hpp"
#include <future>
#include <vector>
#include <algorithm>
#include <mutex>
#include <shared_mutex>

namespace emittra {

Emittra::Emittra(size_t initialQueueSize) 
    : events_mutex(), events(), initial_queue_size(initialQueueSize) {}

Emittra::EventData::EventData(size_t initialSize) 
    : listeners(), 
      event_queue(initialSize), 
      mutex(), 
      cv(), 
      associated_cvs() {}

void Emittra::on(const std::string& event_name, EventCallback callback) {
    auto event_data = get_or_create_event_data(event_name);
    std::unique_lock<std::shared_mutex> lock(event_data->mutex);
    event_data->listeners.push_back(std::move(callback));
}

void Emittra::emit(const std::string& event_name, const std::vector<std::any>& args) {
    auto event_data = get_or_create_event_data(event_name);
    event_data->event_queue.enqueue({args, nullptr});
}

void Emittra::emit(const std::string& event_name, const std::vector<std::any>& args, bool notify) {
    auto event_data = get_or_create_event_data(event_name);
    event_data->event_queue.enqueue({args, nullptr});
    if (notify) {
        event_data->cv.notify_one();
        notify_associated_cvs(event_data);
    }
}

void Emittra::emit_with_token(const std::string& event_name, moodycamel::ProducerToken& token, const std::vector<std::any>& args) {
    auto event_data = get_or_create_event_data(event_name);
    event_data->event_queue.enqueue(token, {args, nullptr});
    event_data->cv.notify_one();
    notify_associated_cvs(event_data);
}

std::future<std::any> Emittra::request(const std::string& event_name, const std::vector<std::any>& args) {
    auto event_data = get_or_create_event_data(event_name);
    auto response_promise = std::make_shared<std::promise<std::any>>();
    auto future = response_promise->get_future();
    event_data->event_queue.enqueue({args, response_promise});
    event_data->cv.notify_one();
    notify_associated_cvs(event_data);
    return future;
}

std::any Emittra::blockingRequest(const std::string& event_name, const std::vector<std::any>& args, std::chrono::milliseconds timeout) {
    auto future = request(event_name, args);
    flush({event_name});
    if (future.wait_for(timeout) == std::future_status::ready) {
        return future.get();
    }
    throw std::runtime_error("Request timed out");
}

void Emittra::remove_listener(const std::string& event_name, const EventCallback& callback) {
    auto event_data = get_or_create_event_data(event_name);
    std::unique_lock<std::shared_mutex> lock(event_data->mutex);
    auto& event_listeners = event_data->listeners;
   event_listeners.erase(
        std::remove_if(event_listeners.begin(), event_listeners.end(),
            [&callback](const EventCallback& cb) {
                return cb.target_type() == callback.target_type();
            }),
        event_listeners.end()
    );
}

void Emittra::remove_all_listeners(const std::string& event_name) {
    auto event_data = get_or_create_event_data(event_name);
    std::unique_lock<std::shared_mutex> lock(event_data->mutex);
    event_data->listeners.clear();
}

size_t Emittra::listener_count(const std::string& event_name) const {
    std::shared_lock<std::shared_mutex> lock(events_mutex);
    if (auto it = events.find(event_name); it != events.end()) {
        std::shared_lock<std::shared_mutex> event_lock(it->second->mutex);
        return it->second->listeners.size();
    }
    return 0;
}

void Emittra::flush(const std::vector<std::string>& event_names) {
    if (event_names.empty()) {
        std::shared_lock<std::shared_mutex> lock(events_mutex);
        for (auto& [event_name, event_data] : events) {
            process_queue(event_data);
        }
    } else {
        for (const auto& event_name : event_names) {
            auto event_data = get_or_create_event_data(event_name);
            process_queue(event_data);
        }
    }
}

std::shared_ptr<std::condition_variable_any> Emittra::make_cv(const std::vector<std::string>& event_names) {
    auto cv = std::make_shared<std::condition_variable_any>();
    for (const auto& event_name : event_names) {
        auto event_data = get_or_create_event_data(event_name);
        std::unique_lock<std::shared_mutex> lock(event_data->mutex);
        event_data->associated_cvs.push_back(cv);
    }
    return cv;
}

void Emittra::wait_for_event(const std::shared_ptr<std::condition_variable_any>& cv, std::chrono::milliseconds timeout) {
    std::shared_mutex dummy_mutex;
    std::unique_lock<std::shared_mutex> lock(dummy_mutex);
    cv->wait_for(lock, timeout);
}

bool Emittra::try_dequeue(const std::string& event_name, std::vector<std::any>& args) {
    auto event_data = get_or_create_event_data(event_name);
    QueuedEvent event;
    if (event_data->event_queue.try_dequeue(event)) {
        args = std::move(event.args);
        return true;
    }
    return false;
}

bool Emittra::try_dequeue_with_token(const std::string& event_name, moodycamel::ConsumerToken& token, std::vector<std::any>& args) {
    auto event_data = get_or_create_event_data(event_name);
    QueuedEvent event;
    if (event_data->event_queue.try_dequeue(token, event)) {
        args = std::move(event.args);
        return true;
    }
    return false;
}

bool Emittra::enqueue_bulk(const std::string& event_name, const std::vector<std::vector<std::any>>& bulk_args) {
    auto event_data = get_or_create_event_data(event_name);
    
    std::vector<QueuedEvent> events;
    events.reserve(bulk_args.size());

    for (const auto& args : bulk_args) {
        events.emplace_back(QueuedEvent{args, nullptr});
    }

    return event_data->event_queue.enqueue_bulk(events.data(), events.size());
}

bool Emittra::enqueue_bulk(const std::string& event_name, const std::vector<std::vector<std::any>>& bulk_args, bool notify){
    bool result = Emittra::enqueue_bulk(event_name, bulk_args);
    auto event_data = get_or_create_event_data(event_name);
    event_data->cv.notify_one();
    notify_associated_cvs(event_data);
    return result;
}

size_t Emittra::try_dequeue_bulk(const std::string& event_name, std::vector<std::vector<std::any>>& bulk_args, size_t max_items) {
    auto event_data = get_or_create_event_data(event_name);
    std::vector<QueuedEvent> events(max_items);
    moodycamel::ConsumerToken token(event_data->event_queue);
    size_t dequeued = event_data->event_queue.try_dequeue_bulk(token, events.begin(), max_items);
    bulk_args.clear();
    bulk_args.reserve(dequeued);
    for (size_t i = 0; i < dequeued; ++i) {
        bulk_args.push_back(std::move(events[i].args));
    }
    return dequeued;
}

std::shared_ptr<Emittra::EventData> Emittra::get_or_create_event_data(const std::string& event_name) {
    {
        std::shared_lock<std::shared_mutex> lock(events_mutex);
        if (auto it = events.find(event_name); it != events.end() && it->second) {
            return it->second;
        }
    }
    std::unique_lock<std::shared_mutex> lock(events_mutex);
    auto& event_data = events[event_name];
    if (!event_data) {
        event_data = std::make_shared<EventData>(initial_queue_size);
    }
    return event_data;
}

void Emittra::process_queue(const std::shared_ptr<EventData>& event_data) {
    constexpr size_t BULK_SIZE = 50000;
    moodycamel::ConsumerToken token(event_data->event_queue);
    std::vector<QueuedEvent> events;
    events.reserve(BULK_SIZE);

    size_t dequeued_count = event_data->event_queue.try_dequeue_bulk(token, std::back_inserter(events), BULK_SIZE);

    if (dequeued_count == 0) return;

    std::vector<EventCallback> listeners_copy;
    {
        std::shared_lock<std::shared_mutex> lock(event_data->mutex);
        listeners_copy = event_data->listeners;
    }

    if (listeners_copy.empty()) {
     for (auto&& event : events) {
            if (event.response_promise) {
                event.response_promise->set_value(std::any());
            }
        }
        event_data->cv.notify_all();
        notify_associated_cvs(event_data);
        return;
    }

    bool any_response = std::any_of(events.begin(), events.end(),
        [](const QueuedEvent& e) { return e.response_promise != nullptr; });

    if (any_response) {
        for (auto&& event : events) {
            bool response_set = false;
            auto respond_func = [&response_set, &event](const std::any& response) {
                if (event.response_promise && !response_set) {
                    event.response_promise->set_value(response);
                    response_set = true;
                }
            };

            for (const auto& callback : listeners_copy) {
                std::invoke(callback, respond_func, event.args);
            }

            if (event.response_promise && !response_set) {
                event.response_promise->set_value(std::any());
            }
        }
    } else {
    auto dummy_respond = [](const std::any&) {};
        for (auto&& event : events) {
            for (const auto& callback : listeners_copy) {
                std::invoke(callback, dummy_respond, event.args);
            }
        }
    }

    event_data->cv.notify_all();
    notify_associated_cvs(event_data);
}

void Emittra::notify_associated_cvs(const std::shared_ptr<EventData>& event_data) {
  std::unique_lock<std::shared_mutex> lock(event_data->mutex);
    auto& cvs = event_data->associated_cvs;
    cvs.erase(
        std::remove_if(cvs.begin(), cvs.end(), 
            [](const std::weak_ptr<std::condition_variable_any>& weak_cv) {
                if (auto cv = weak_cv.lock()) {
                    cv->notify_all();
                    return false;
                }
                return true;
            }),
        cvs.end()
    );
}

} // namespace emittra