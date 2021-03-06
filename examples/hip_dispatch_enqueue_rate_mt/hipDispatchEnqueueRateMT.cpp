/* -----------------------------------------------------------------------------
 * Copyright (c) 2020 Advanced Micro Devices, Inc. All Rights Reserved.
 * See 'LICENSE' in the project root for license information.
 * -------------------------------------------------------------------------- */
#include "hip/hip_runtime.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <future>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#define NUM_GROUPS 1
#define GROUP_SIZE 1
#define WARMUP_RUN_COUNT 10
#define TIMING_RUN_COUNT 100
#define TOTAL_RUN_COUNT WARMUP_RUN_COUNT + TIMING_RUN_COUNT

__global__ void EmptyKernel() {}

// Helper to print various timing metrics
void print_timing(std::string test, std::array<float, TOTAL_RUN_COUNT> &results, int batch = 1)
{

    float total_us = 0.0f, mean_us = 0.0f, stddev_us = 0.0f;

    // remove top outliers due to nature of variability across large number of multi-threaded runs
    std::sort(results.begin(), results.end(), std::greater<float>());
    auto start_iter = std::next(results.begin(), WARMUP_RUN_COUNT);
    auto end_iter = results.end();

    // mean
    std::for_each(start_iter, end_iter, [&](const float &run_ms) {
        total_us += (run_ms * 1000) / batch;
    });
    mean_us = total_us  / TIMING_RUN_COUNT;

   // stddev
    total_us = 0;
    std::for_each(start_iter, end_iter, [&](const float &run_ms) {
        float dev_us = ((run_ms * 1000) / batch) - mean_us;
        total_us += dev_us * dev_us;
    });
    stddev_us = sqrt(total_us / TIMING_RUN_COUNT);

    printf("\n %s: %.1f us, std: %.1f us\n", test.c_str(), mean_us, stddev_us);
}

// Measure time taken to enqueue a kernel on the GPU using hipModuleLaunchKernel
// void hipModuleLaunchKernel_enqueue_rate(std::atomic_int* shared, int max_threads)
// {
//     //resources necessary for this thread
//     hipStream_t stream;
//     hipStreamCreate(&stream);
//     hipModule_t module;
//     hipFunction_t function;
//     hipModuleLoad(&module, "test_kernel.code");
//     hipModuleGetFunction(&function, module, "test");
//     void* kernel_params = nullptr;
//     std::array<float, TOTAL_RUN_COUNT> results;

//     //synchronize all threads, before running
//     int tid = shared->fetch_add(1, std::memory_order_release);
//     while (max_threads != shared->load(std::memory_order_acquire)) {}

//     for (auto i = 0; i < TOTAL_RUN_COUNT; ++i) {
//         auto start = std::chrono::high_resolution_clock::now();
//         hipModuleLaunchKernel(function, 1, 1, 1, 1, 1, 1, 0, stream, &kernel_params, nullptr);
//         auto stop = std::chrono::high_resolution_clock::now();
//         results[i] = std::chrono::duration<double, std::milli>(stop - start).count();
//     }
//     print_timing("Thread ID : " + std::to_string(tid) + " , " + "hipModuleLaunchKernel enqueue rate", results);
// }

// Measure time taken to enqueue a kernel on the GPU using hipLaunchKernelGGL
void hipLaunchKernelGGL_enqueue_rate(std::atomic_int* shared, int max_threads)
{
    //resources necessary for this thread
    hipStream_t stream;
    hipStreamCreate(&stream);
    std::array<float, TOTAL_RUN_COUNT> results;

    //synchronize all threads, before running
    int tid = shared->fetch_add(1, std::memory_order_release);
    while (max_threads != shared->load(std::memory_order_acquire)) {}

    for (auto i = 0; i < TOTAL_RUN_COUNT; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        hipLaunchKernelGGL((EmptyKernel), dim3(NUM_GROUPS), dim3(GROUP_SIZE), 0, stream);
        auto stop = std::chrono::high_resolution_clock::now();
        results[i] = std::chrono::duration<float, std::milli>(stop - start).count();
    }
    print_timing("Thread ID : " + std::to_string(tid) + " , " + "hipLaunchKernelGGL enqueue rate", results);
}

// Simple thread pool
struct thread_pool {
    thread_pool(int total_threads) : max_threads(total_threads) {}
    void start(std::function<void(std::atomic_int*, int)> f) {
        for (int i = 0; i < max_threads; ++i) {
            threads.push_back(std::async(std::launch::async, f, &shared, max_threads));
        }
    }
    void finish() {
        for (auto&&thread : threads) {
            thread.get();
        }
        threads.clear();
        shared = {};
    }
    ~thread_pool() {
        finish();
    }
private:
    std::atomic_int shared {0};
    std::vector<std::future<void>> threads;
    int max_threads = 1;
};


int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Run test as 'hipDispatchEnqueueRateMT <num_threads> <0-hipModuleLaunchKernel /1-hipLaunchKernelGGL>'\n";
        return -1;
    }

    int max_threads = atoi(argv[1]);
    int run_module_test = atoi(argv[2]);
    if(max_threads < 1 || run_module_test < 0 || run_module_test > 1) {
        std::cerr << "Invalid Input.\n";
        std::cerr << "Run test as 'hipDispatchEnqueueRateMT <num_threads> <0-hipModuleLaunchKernel /1-hipLaunchKernelGGL>'\n";
        return -1;
    }
    thread_pool task(max_threads);

    // if(run_module_test == 0) {
    //     task.start(hipModuleLaunchKernel_enqueue_rate);
    //     task.finish();
    // } else {
        task.start(hipLaunchKernelGGL_enqueue_rate);
        task.finish();
    // }

    return 0;
}