#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../include/utilities/mpmc_dictionary.h"
#include "../../include/arena.h"

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <thread>
#include <vector>
#include <memory>
#include <iostream>

using namespace std;

UnitTest unit_test;

struct AllocationMetadata
{
    std::size_t size = 0;
    std::size_t padding_bytes = 0;
};

using HashmapType = MPMCDictionary<uint64_t, AllocationMetadata, typename Arena::MetadataAllocator>;

int main(int argc, char* argv[])
{
    //////////////////////////////////////////////////////////////
    // SINGLE THREADED
    {
        HashmapType dict;
        std::size_t SCALE = 4;
        std::size_t dict_capacity = 655360 / sizeof(typename HashmapType::DictionaryNode);

        if (dict.initialise(dict_capacity) == false)
        {
            std::cout << "Dict init failed\n";
            return -1;
        }

        for (std::size_t i = 0; i < dict_capacity * SCALE; i++)
        {
            dict.insert(i, { i, i });

            AllocationMetadata metadata;
            dict.get(i, metadata);

            // unit_test.test_equals(metadata.padding_bytes == i && metadata.size == i, true, "mpmc dictionary", "insertion and positive retrieval" + std::to_string(i+1)); // console prints taking too long
            if (metadata.padding_bytes != i || metadata.size != i)
            {
                std::cout << "Dict init failed\n";
                return -1;
            }

            bool retrieval_result = dict.get(i+1, metadata);

            // unit_test.test_equals(retrieval_result, false, "mpmc dictionary", "negative retrieval " + std::to_string(i + 1)); // console prints taking too long
            if (retrieval_result != false)
            {
                std::cout << "Dict retrieval failed\n";
                return -1;
            }
        }
    }
    
    //////////////////////////////////////////////////////////////
    // MULTI THREADED
    {
        HashmapType dict;
        std::size_t dict_capacity = 655360 / sizeof(typename HashmapType::DictionaryNode);

        if (dict.initialise(dict_capacity) == false)
        {
            std::cout << "Dict init failed\n";
            return -1;
        }

        std::atomic<std::size_t> inserted_key_count = 0;
        std::atomic<bool> producers_exiting = false;
        std::atomic<bool> consumers_exiting = false;

        std::size_t TARGET_KEY_COUNT = dict_capacity * 20;

        std::size_t num_producer_threads = 8;
        std::size_t num_consumer_threads = 8;

        auto procuder_thread_function = [&]()
            {
                while (!producers_exiting)
                {
                    std::size_t key = inserted_key_count.fetch_add(1, std::memory_order_relaxed);

                    AllocationMetadata metadata = { key, key };
                    dict.insert(key, metadata);
                }
            };

        auto consumer_thread_function = [&](std::size_t index)
            {
                while (!consumers_exiting)
                {
                    auto available_key_count = inserted_key_count.load();

                    for (std::size_t i = 0; i < available_key_count; i++)
                    {
                        AllocationMetadata metadata;

                        if (dict.get(i, metadata))
                        {
                            if (metadata.size != i || metadata.padding_bytes != i)
                            {
                                std::cout << "Data mismatch for key: " << i << "\n";
                                std::abort();
                            }
                        }
                    }

                    std::cout << "Consumer " << index << " finished verifying " << available_key_count << " keys\n";
                }
            };

        std::vector<std::unique_ptr<std::thread>> producer_threads;
        std::vector<std::unique_ptr<std::thread>> consumer_threads;

        for (std::size_t i = 0; i < num_producer_threads; i++)
        {
            producer_threads.emplace_back(new std::thread(procuder_thread_function));
        }

        for (std::size_t i = 0; i < num_consumer_threads; i++)
        {
            consumer_threads.emplace_back(new std::thread(consumer_thread_function, i));
        }

        while (true)
        {
            std::cout << "Current key count : " << inserted_key_count << " target key count : " << TARGET_KEY_COUNT << " \n";

            if (inserted_key_count.load() >= TARGET_KEY_COUNT)
            {
                producers_exiting = true;
                break;
            }
        }

        // Lets make consumers work longer

        for (std::size_t i = 0; i < 5; i++)
        {
            std::cout << "Wait " << i << " of 5 seconds \n";
            std::this_thread::sleep_for(std::chrono::seconds(1)); 
        }
        
        consumers_exiting = true;

        for (auto& producer : producer_threads)
        {
            producer->join();
        }

        for (auto& consumer : consumer_threads)
        {
            consumer->join();
        }
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("mpmc dictionary");
    std::cout.flush();
    
    #if _WIN32
    bool pause = true;
    if(argc > 1)
    {
        if (std::strcmp(argv[1], "no_pause") == 0)
            pause = false;
    }
    if(pause)
        std::system("pause");
    #endif

    return unit_test.did_all_pass();
}