#include "emittra.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

const int NUM_THREADS = 16;
std::atomic<bool> stop_processing{false};

// Using atomics for safe concurrent increments.
std::atomic<int> events_total_count[NUM_THREADS];

void emitter_thread(emittra::Emittra& emitter) {
    // Precompute event names.
    std::vector<std::string> event_names;
    event_names.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++) {
        event_names.push_back("benchmark_event" + std::to_string(i));
    }
    
    // Preconstruct a bulk events vector with 50000 empty event arguments.
    const std::vector<std::vector<std::any>> bulk_events(50000);

    while (!stop_processing.load(std::memory_order_relaxed)) {
        for (int i = 0; i < NUM_THREADS; i++) {
            emitter.enqueue_bulk(event_names[i], bulk_events);
        }
    }
}

void worker(emittra::Emittra& emitter, int thread_id) {
    std::string event_name = "benchmark_event" + std::to_string(thread_id);
    // Precompute flush vector to avoid reallocations.
    std::vector<std::string> flush_vector = { event_name };

    // Register a listener that increments the thread-specific counter.
    emitter.on(event_name, [thread_id](const auto& respond, const auto& args) {
        events_total_count[thread_id]++;
    });

    while (!stop_processing.load(std::memory_order_relaxed)) {
        emitter.flush(flush_vector);
    }
    
    // Final flush and cleanup.
    emitter.flush(flush_vector);
    emitter.remove_all_listeners(event_name);
}

int main() {
    emittra::Emittra emitter(1024);
    
    // Initialize the counters.
    for (int i = 0; i < NUM_THREADS; i++) {
        events_total_count[i] = 0;
    }
    
    std::thread emit_thread(emitter_thread, std::ref(emitter));

    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(worker, std::ref(emitter), i);
    }

    auto start = std::chrono::high_resolution_clock::now();

    // Run benchmark for 10 seconds.
    std::this_thread::sleep_for(std::chrono::seconds(10));
    stop_processing.store(true, std::memory_order_relaxed);

    emit_thread.join();
    for (auto& t : workers) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int total_processed = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_processed += events_total_count[i].load(std::memory_order_relaxed);
    }

    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    std::cout << "Total events processed: " << total_processed << std::endl;
    std::cout << "Events per second: " << (total_processed * 1000.0 / duration.count()) << std::endl;

    return 0;
}
