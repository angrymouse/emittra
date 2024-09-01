#include "emittra.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

const int NUM_THREADS = 12;
bool stop_processing = false;


int events_total_count[NUM_THREADS] = {};

void emitter_thread(emittra::Emittra& emitter) {

    while (!stop_processing) {
        for (int i=0;i<NUM_THREADS;i++){
        std::string event_name_str = "benchmark_event" + std::to_string(i);
        const char* event_name = event_name_str.c_str();
        emitter.enqueue_bulk(event_name, std::vector<std::vector<std::any>>(50000));
        }   
    }
}

void worker(emittra::Emittra& emitter, int thread_id) {
    std::string event_name_str = "benchmark_event" + std::to_string(thread_id);
    const char* event_name = event_name_str.c_str();

    emitter.on(event_name, [thread_id](const auto& respond, const auto& args) {
        events_total_count[thread_id]++;
    });

    while (!stop_processing) {
        emitter.flush({event_name});

    }


    emitter.flush({event_name});

    emitter.remove_all_listeners(event_name);
}

int main() {
    emittra::Emittra emitter(1048576); 

   
    std::thread emit_thread(emitter_thread, std::ref(emitter));

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, std::ref(emitter), i);
    }


    auto start = std::chrono::high_resolution_clock::now();

   
    std::this_thread::sleep_for(std::chrono::seconds(10));

    
    stop_processing = true;

   
    emit_thread.join();
    

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int total_processed = 0;
    for (int i : events_total_count) {
        total_processed += i;
    }

    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    std::cout << "Total events processed: " << total_processed << std::endl;
    std::cout << "Events per second: " << (total_processed * 1000.0 / duration.count()) << std::endl;

    return 0;
}