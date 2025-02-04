/*
    Provides :

                static unsigned int get_number_of_logical_cores()
                static unsigned int get_number_of_physical_cores()
                static bool is_hyper_threading()
                static inline void yield()

*/
#ifndef _THREAD_UTILITIES_H_
#define _THREAD_UTILITIES_H_

#ifdef __linux__        // VOLTRON_EXCLUDE
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#elif _WIN32            // VOLTRON_EXCLUDE
#include <windows.h>
#include <chrono>
#include <thread>
#endif                    // VOLTRON_EXCLUDE

#include <string_view>
#include <type_traits>

/*
    Currently this module is not hybrid-architecture-aware
    Ex: P-cores and E-cores starting from Alder Lake
    That means all methods assume that all CPU cores are identical
*/

class ThreadUtilities
{
    public:

        static unsigned int get_number_of_logical_cores()
        {
            unsigned int num_cores{0};
            #ifdef __linux__
            num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            #elif _WIN32
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            num_cores = sysinfo.dwNumberOfProcessors;
            #endif
            return num_cores;
        }

        static unsigned int get_number_of_physical_cores()
        {
            auto num_logical_cores = get_number_of_logical_cores();
            bool cpu_hyperthreading = is_hyper_threading();
            return cpu_hyperthreading ? num_logical_cores / 2 : num_logical_cores;
        }

        static bool is_hyper_threading()
        {
            bool ret = false;

            #ifdef __linux__
            // Using syscalls to avoid dynamic memory allocation
            int file_descriptor = open("/sys/devices/system/cpu/smt/active", O_RDONLY);

            if (file_descriptor != -1)
            {
                char value;
                if (read(file_descriptor, &value, sizeof(value)) > 0)
                {
                    int smt_active = value - '0';
                    ret = (smt_active > 0);
                }

                close(file_descriptor);
            }
            #elif _WIN32
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            char buffer[2048]; // It may be insufficient however even if one logical processor has SMT flag , it means we are hyperthreading
            DWORD buffer_size = sizeof(buffer);

            GetLogicalProcessorInformation(reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer), &buffer_size);

            DWORD num_system_logical_processors = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (DWORD i = 0; i < num_system_logical_processors; ++i)
            {
                if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->Relationship == RelationProcessorCore)
                {
                    if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->ProcessorCore.Flags == LTP_PC_SMT)
                    {
                        ret = true;
                        break;
                    }
                }
            }
            #endif
            return ret;
        }

        static inline void yield()
        {
            #ifdef __linux__
            sched_yield();
            #elif _WIN32
            SwitchToThread();
            #endif
        }

    private:
};

#endif