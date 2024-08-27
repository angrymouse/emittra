# Emittra - Advanced Framework for Event-Driven Programming in C++

Emittra is a high-performance, thread-first C++ framework/library for event-driven programming. It provides rich feature set for building clear/maintainable applications that scale.


## Features

- Thread-safe event emission and handling of events across the threads
- Fine-grained control over event processing and event loops
- Concurrent event loops running on lock-free queue 
- Flexible event listener management
- Request/response ability, allowing event handler to respond to event


## Performance

Emittra could handle to 2.5 million events per second on a 6-core AMD Ryzen 5 1600 processor, but more community benchmarks are welcome. To contribute one, see [Contributing](#contributing)

## Requirements

- C++17 compatible compiler
- CMake (version 3.10 or higher)

## Building Emittra

1. Clone the repository:
   ```
   git clone https://codeberg.org/EleutheriaDeutera/emittra
   cd emittra
   ```

2. Create a build directory:
   ```
   mkdir build && cd build
   ```

3. Run CMake and build:
   ```
   cmake ..
   make
   ```

## Usage

Examples of using Emittra in C++ projects:

### Basic Usage

This example shows "fire-and-forget" event, that is registered, called, processed (flushed) on single thread. 

```cpp
#include "emittra/emittra.hpp"
#include <iostream>
#include <string>

int main() {
    emittra::Emittra emitter;

    // Register an event listener
    emitter.on("greet", [](const emittra::Emittra::RespondFunction&, const std::vector<std::any>& args) {
        std::cout << "Hello, " << std::any_cast<std::string>(args[0]) << "!" << std::endl;
    });

    // Emit an event
    emitter.emit("greet", {"World"s});

    // Process the emitted events
    emitter.flush();

    return 0;
}
```

### Request/response
This example shows how event handlers can respond to events they receive.

```cpp
#include "emittra/emittra.hpp"
#include <iostream>
#include <future>

int main() {
    emittra::Emittra emitter;

    emitter.on("calculate", [](const emittra::Emittra::RespondFunction& respond, const std::vector<std::any>& args) {
        auto [a,b] = emittra::demarshall_args<int,int>(args);
        respond(a + b);
    });

    auto result = emitter.request("calculate", {5, 3});

    // Process the request
    emitter.flush();

    std::cout << "Result: " << std::any_cast<int>(result.get()) << std::endl;

    return 0;
}
```

### Multithreaded Example

Emittra is not only thread-safe, it's thread-first, meaning the recommended way to listen to events is on foreign threads.
Following example shows how to run event loop on worker thread and process "data" event on it - it can be any number of any events, but it's recommended to not just use 1 thread with way too many event listeners. 

```cpp
#include "emittra/emittra.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

int main() {
    emittra::Emittra emitter;
    std::atomic<bool> stop_processing{false};

 

    std::thread worker([&emitter, &stop_processing]() {
        // Register the event listener
        emitter.on("data", [](const auto& respond, const auto& args) {
            auto [value] = emittra::demarshall_args<int>(args);
            std::cout << "Worker processed data: " << value << std::endl;
            respond(value * 2);
        });
        //You could register any number of events to run on this thread, just make sure you include them into regular flush.
        auto events_cv=emitter.make_cv({"data"});
        while (!stop_processing) {
            emitter.flush({"data"});
            emitter.wait_for_event(events_cv, std::chrono::milliseconds(100));
        }
        emitter.flush({"data"});// After stopped, we still want to do "last flush"

        // Remove all listeners for "data" event when done. Not doing so will result to memory leak.
        emitter.remove_all_listeners("data");
    });
    //Fire-and-forget emits
    emitter.emit("data", {1});
    emitter.emit("data", {2});
    emitter.emit("data", {3});

    // Make a request
    auto future = emitter.request("data", {4});
    auto result = std::any_cast<int>(future.get());
    std::cout << "Main received result: " << result << std::endl;

    stop_processing = true;
    worker.join();

    return 0;
}
```


## API Reference

### Constructor

- `Emittra(size_t initialQueueSize = 100)`: Creates an Emittra instance with the specified initial queue size.

### Event Handling

- `void on(const std::string& event_name, EventCallback callback)`: Registers an event listener.
- `void emit(const std::string& event_name, const std::vector<std::any>& args = {})`: Emits an event.
- `void emit_with_token(const std::string& event_name, moodycamel::ProducerToken& token, const std::vector<std::any>& args = {})`: Emits an event using a producer token for improved performance.
- `std::future<std::any> request(const std::string& event_name, const std::vector<std::any>& args = {})`: Sends an asynchronous request and returns a future.
- `std::any blockingRequest(const std::string& event_name, const std::vector<std::any>& args = {}, std::chrono::milliseconds timeout = std::chrono::seconds(5))`: Sends a blocking request with an optional timeout.

### Listener Management

- `void remove_listener(const std::string& event_name, const EventCallback& callback)`: Removes a specific listener.
- `void remove_all_listeners(const std::string& event_name)`: Removes all listeners for an event.
- `size_t listener_count(const std::string& event_name) const`: Returns the number of listeners for an event.

### Queue Management

- `void flush(const std::vector<std::string>& event_names = {})`: Processes all queued events for specified event names (or all events if not specified).
- `std::shared_ptr<std::condition_variable_any> make_cv(const std::vector<std::string>& event_names)`: Creates a condition variable associated with specified events.
- `void wait_for_event(const std::shared_ptr<std::condition_variable_any>& cv, std::chrono::milliseconds timeout = std::chrono::milliseconds::max())`: Waits for an event to occur.

### Bulk Operations

- `bool enqueue_bulk(const std::string& event_name, const std::vector<std::vector<std::any>>& bulk_args)`: Enqueues multiple events at once.
- `size_t try_dequeue_bulk(const std::string& event_name, std::vector<std::vector<std::any>>& bulk_args, size_t max_items)`: Attempts to dequeue multiple events at once.

## License

AGPL, usage in closed-source projects is strictly prohibited unless explicitly allowed by authors. If you are interested in using Emittra in your closed-source, commercial project, please refer to [contact](#contact) for pricing. All proceeds will go to frequent contributors of Emittra.

## Contributing

Issues, merge requests are welcome! 

Development happens on codeberg - [Emittra](https://codeberg.org/EleutheriaDeutera/emittra)  

## Contact

Eleutheria Deutera <eleutheriadeutera@cock.li>