#ifndef _STRESS_TEST_RUNNER_H_
#define _STRESS_TEST_RUNNER_H_

#include <llmalloc.h>

#define allocate_function(size) llmalloc::ScalableMalloc::get_instance().allocate(size)
#define deallocate_function(ptr) llmalloc::ScalableMalloc::get_instance().deallocate(ptr)
#define get_usable_size_function(ptr) llmalloc::ScalableMalloc::get_instance().get_usable_size(ptr) // Most Linux apps use realloc extensively ,therefore get_usable_size is a critical function

#include "stress_test_options.h"


#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <thread>
#include <memory>
#include <vector>
#include <string>
#include <iostream>

class StressTestRunner
{
    public:
        
        void run(const StressTestOptions& o = StressTestOptions())
        {
            pointers_lock.initialise();
            print_lock.initialise();

            options = o;

            if (llmalloc::ScalableMalloc::get_instance().create(options.scalable_malloc_options) == false)
            {
                throw std::runtime_error("llmalloc creation failed");
            }

            for (std::size_t i = 0; i < options.thread_count; i++)
            {
                m_threads.emplace_back(new std::thread(StressTestRunner::thread_function, i));
            }
        }
        
        void join()
        {
            for (auto& thread : m_threads)
            {
                thread->join();
            }
        }

    private:
        static inline StressTestOptions options;
        std::vector<std::unique_ptr<std::thread>> m_threads;

        struct Pointer
        {
            std::size_t allocating_thread_index = 0;
            void* address = nullptr;
            bool deallocated = false;
            std::size_t allocation_size = 0;
        };

        static inline std::vector<Pointer> pointers;
        static inline llmalloc::UserspaceSpinlock<> pointers_lock;
        static inline llmalloc::UserspaceSpinlock<> print_lock;

        static inline std::atomic<bool> failure_exit = false;

        static inline void thread_safe_print(const std::string& message)
        {
            #ifdef ENABLE_CONSOLE_PRINT
            print_lock.lock();

            std::cout << message << "\n";

            print_lock.unlock();
            #endif
        }

        static inline void register_pointer(void* ptr, std::size_t allocating_thread_index, std::size_t allocation_size)
        {
            pointers_lock.lock();

            Pointer p;
            p.allocating_thread_index = allocating_thread_index;
            p.address = ptr;
            p.allocation_size = allocation_size;

            pointers.push_back(p);

            pointers_lock.unlock();
        }

        static void thread_function(std::size_t thread_index)
        {
            thread_safe_print("Thread " + std::to_string(thread_index) + " starting");

            for (std::size_t i = 0; i < options.iterations; i++)
            {
                bool is_op_allocation = true;
                std::size_t alloc_counter = 0;
                std::size_t dealloc_counter = 0;
                std::size_t allocation_job_count = options.size_classes.size();

                auto do_single_iteration = [&]()
                    {
                        while (true)
                        {
                            if (failure_exit)
                            {
                                return;
                            }

                            if (is_op_allocation)
                            {
                                // ALLOCATING
                                auto allocation_size_class_index = alloc_counter % allocation_job_count;
                                auto allocation_size = options.size_classes[allocation_size_class_index];

                                auto ptr = allocate_function(allocation_size);

                                if (ptr == nullptr)
                                {
                                    failure_exit = true;
                                    fprintf(stderr, "Alloc failed , thread index = %zu, size = %zu,\n", thread_index, allocation_size);
                                    return;
                                }

                                // Access all the bytes
                                if (allocation_size <= options.max_size_for_data_verifications)
                                {
                                    builtin_memset(ptr, 'x', allocation_size);
                                }

                                register_pointer(ptr, thread_index, allocation_size);

                                alloc_counter++;
                            }
                            else
                            {
                                pointers_lock.lock();

                                if (options.cross_thread_deallocations == false || options.thread_count == 1)
                                {
                                    // DEALLOCATING OWN PTRS
                                    for (auto it = pointers.begin(); it != pointers.end(); ++it)
                                    {
                                        auto& ptr = *it;

                                        if (ptr.deallocated == false)
                                        {
                                            if (ptr.allocating_thread_index == thread_index)
                                            {
                                                auto queried_usable_size = get_usable_size_function(ptr.address);

                                                if (queried_usable_size < ptr.allocation_size)
                                                {
                                                    failure_exit = true;
                                                    fprintf(stderr, "get_usable_size failed, thread index = %zu, size = %zu,\n", thread_index, ptr.allocation_size);
                                                    pointers_lock.unlock();
                                                    return;
                                                }

                                                // Verify all bytes
                                                if (queried_usable_size <= options.max_size_for_data_verifications)
                                                {
                                                    for (std::size_t n = 0; n < queried_usable_size; n++)
                                                    {
                                                        if (((char*)ptr.address)[n] != 'x')
                                                        {
                                                            failure_exit = true;
                                                            fprintf(stderr, "Data verification failed on own ptr, thread index = %zu, size = %zu,\n", thread_index, ptr.allocation_size);
                                                            pointers_lock.unlock();
                                                            return;
                                                        }
                                                    }
                                                }

                                                deallocate_function(ptr.address);
                                                ptr.deallocated = true;

                                                pointers.erase(it);
                                                break;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    // DEALLOCATING OTHER THREADS' PTRS
                                    for (auto it = pointers.begin(); it != pointers.end(); ++it)
                                    {
                                        auto& ptr = *it;

                                        if (ptr.deallocated == false)
                                        {
                                            if (ptr.allocating_thread_index != thread_index)
                                            {
                                                auto queried_usable_size = get_usable_size_function(ptr.address);

                                                if (queried_usable_size < ptr.allocation_size)
                                                {
                                                    failure_exit = true;
                                                    fprintf(stderr, "get_usable_size failed, thread index = %zu, size = %zu,\n", thread_index, ptr.allocation_size);
                                                    pointers_lock.unlock();
                                                    return;
                                                }

                                                deallocate_function(ptr.address);
                                                ptr.deallocated = true;
                                                pointers.erase(it);
                                                break;
                                            }
                                        }
                                    }
                                }

                                pointers_lock.unlock();
                                dealloc_counter++;
                            }


                            if ((alloc_counter + dealloc_counter) % options.op_interleave_period == 0)
                            {
                                is_op_allocation = !is_op_allocation;
                            }

                            if (dealloc_counter >= allocation_job_count)
                            {
                                return;
                            }
                        }
                    };

                do_single_iteration();

                thread_safe_print("Thread " + std::to_string(thread_index) + " iteration " + std::to_string(i) + " of " + std::to_string(options.iterations) + " completed");
            }

            thread_safe_print("Thread " + std::to_string(thread_index) + " ending");
        }
};

#endif