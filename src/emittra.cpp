#include "emittra.hpp"
#include <future>

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
    event_data->cv.notify_one();
    notify_associated_cvs(event_data);
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
        events.push_back({args, nullptr});
    }
    bool result = event_data->event_queue.enqueue_bulk(events.begin(), events.size());
    event_data->cv.notify_one();
    notify_associated_cvs(event_data);
    return result;
}

size_t Emittra::try_dequeue_bulk(const std::string& event_name, std::vector<std::vector<std::any>>& bulk_args, size_t max_items) {
    auto event_data = get_or_create_event_data(event_name);
    std::vector<QueuedEvent> events(max_items);
    size_t dequeued = event_data->event_queue.try_dequeue_bulk(events.begin(), max_items);
    bulk_args.clear();
    bulk_args.reserve(dequeued);
    for (size_t i = 0; i < dequeued; ++i) {
        bulk_args.push_back(std::move(events[i].args));
    }
    return dequeued;
}

std::shared_ptr<Emittra::EventData> Emittra::get_or_create_event_data(const std::string& event_name) {
    std::unique_lock<std::shared_mutex> lock(events_mutex);
    auto& event_data = events[event_name];
    if (!event_data) {
        event_data = std::make_shared<EventData>(initial_queue_size);
    }
    return event_data;
}

void Emittra::process_queue(const std::shared_ptr<EventData>& event_data) {
    const size_t BULK_SIZE = 100;
    std::vector<QueuedEvent> events(BULK_SIZE);

    size_t dequeued_count = event_data->event_queue.try_dequeue_bulk(events.begin(), BULK_SIZE);

    if (dequeued_count > 0) {
        std::vector<EventCallback> callbacks;
        {
            std::shared_lock<std::shared_mutex> lock(event_data->mutex);
            callbacks = event_data->listeners; // Copy listeners to avoid holding the lock
        }

        for (size_t i = 0; i < dequeued_count; ++i) {
            const auto& event = events[i];
            std::atomic<bool> response_set(false);

            auto respond_func = [&response_set, promise = event.response_promise](const std::any& response) {
                bool expected = false;
                if (promise && response_set.compare_exchange_strong(expected, true)) {
                    promise->set_value(response);
                }
            };

       
            if (callbacks.size() > 1) {
                std::vector<std::future<void>> futures;
                futures.reserve(callbacks.size());

                for (const auto& callback : callbacks) {
                    futures.emplace_back(std::async(std::launch::async, [&]() {
                            callback(respond_func, event.args);
                    }));
                }

                for (auto& future : futures) {
                    future.wait();
                }
            } else if (callbacks.size() == 1) {
                callbacks[0](respond_func, event.args);
            }
            if (event.response_promise) {
                bool expected = false;
                if (response_set.compare_exchange_strong(expected, true)) {
                    event.response_promise->set_value(std::any());
                }
            }
        }

        event_data->cv.notify_all();
    }
}

void Emittra::notify_associated_cvs(const std::shared_ptr<EventData>& event_data) {
    std::shared_lock<std::shared_mutex> lock(event_data->mutex);
    for (auto it = event_data->associated_cvs.begin(); it != event_data->associated_cvs.end();) {
        if (auto cv = it->lock()) {
            cv->notify_all();
            ++it;
        } else {
            it = event_data->associated_cvs.erase(it);
        }
    }
}

} // namespace emittra
