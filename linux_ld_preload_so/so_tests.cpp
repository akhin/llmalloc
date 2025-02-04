#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <malloc.h> // malloc_usable_size
#include <vector>
#include <iostream>

inline bool validate_buffer(void* buffer, std::size_t buffer_size);
bool is_address_aligned(void* address, std::size_t alignment);

int main()
{
    ////////////////////////////////////////////////////////////////////////////
    // realloc & malloc_usable_size
    //
    // Majority of Linux sw use realloc extensively which relies on get_usable_size
    // for copying the old buffer contents to the new buffer in case buffer of increase
    {
        std::vector<std::size_t> realloc_sizes = { 8, 31, 32, 33 , 32767, 32768, 32769, 262143, 264144, 262145, 600000 };
        
        for(const auto& size : realloc_sizes)
        {
            auto ptr = malloc(size);
            
            if(!ptr)
            {
                std::cout << "Failure malloc, size :" << size << "\n";
                return -1;
            }

            if (!is_address_aligned(ptr, 16)) 
            {
                std::cout << "Failure 16 byte alignment malloc\n";
                free(ptr);
                return -1;
            }

            if (!validate_buffer(ptr, size)) 
            {
                std::cout << "Failure malloc, size :" << size << "\n";
                return -1;
            }

            if( malloc_usable_size(ptr) < size || !malloc_usable_size)
            {
                std::cout << "Failure malloc_usable_size, size :" << size << " malloc usable size : " <<  malloc_usable_size(ptr) << "\n";
                return -1;
            }

            std::memset(ptr, 'j', size);

            auto doubled_size = size*2;

            ptr = realloc(ptr, doubled_size);

            // realloc should copy the old buffer to the new buffer
            for(std::size_t i = 0; i< size;i++)
            {
                if(((char*)ptr)[i] != 'j')
                {
                    std::cout << "Failure realloc data verification , index : " <<  i << " , size :" << size << "\n";
                    return -1;
                }
            }

            if (!validate_buffer(ptr, doubled_size)) 
            {
                std::cout << "Failure realloc , size :" << doubled_size << "\n";
                return -1;
            }

            free(ptr);
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    // reallocarray
    {
        std::size_t count = 5;
        std::size_t size = sizeof(int);
        
        int *arr = (int*)reallocarray(NULL, count, size);
        
        if(!arr)
        {
            std::cout << "Failure reallocarray, count :" << count << " item size : " << size << "\n";
            return -1;
        }
        
        if (!validate_buffer(arr, size*count)) 
        {
            std::cout << "Failure reallocarray, count :" << count << " item size : " << size << "\n";
            return -1;
        }
        
        for(std::size_t i =0; i < count; i++)
        {
            arr[i] = 6;
        }
        
        std::size_t new_count = 10;
        int *new_arr = (int*)reallocarray(arr, new_count, size);
        
        for(std::size_t i =0; i < count; i++)
        {
            if( new_arr[i] != 6 )
            {
                std::cout << "Failure reallocarray, old count :" << count << " new count : " << new_count << "\n";
                free(new_arr);
                return -1;
            }
        }
        
        if (!validate_buffer(new_arr, size*count)) 
        {
            std::cout << "Failure reallocarray, count :" << count << " item size : " << size << "\n";
            return -1;
        }

        free(new_arr);
    }
    ////////////////////////////////////////////////////////////////////////////
    // calloc
    {
        auto ptr = (char*)calloc(42, 42);
        
        for(std::size_t i =0; i < 42*42;i++)
        {
            if(ptr[i] != 0 )
            {
                std::cout << "Failure calloc\n";
                return -1;
            }
        }

        if (!is_address_aligned(ptr, 16)) 
        {
            std::cout << "Failure 16 byte alignment calloc\n";
            free(ptr);
            return -1;
        }

        for(std::size_t i = 0; i< 42*42;i++)
        {
            if(((char*)ptr)[i] != 0)
            {
                std::cout << "Failure calloc data verification , index : " <<  i << "\n";
                return -1;
            }
        }

        if(!validate_buffer(ptr, 42*42))
        {
            std::cout << "Failure calloc\n";
            return -1;
        }

        free(ptr);
    }

    ////////////////////////////////////////////////////////////////////////////
    // posix_memalign
    {
        void* ptr = nullptr;
        if (posix_memalign(&ptr, 128, 64) != 0) 
        {
            std::cout << "Failure posix_memalign\n";
            return -1;
        }

        if (!is_address_aligned(ptr, 128)) 
        {
            std::cout << "Failure posix_memalign\n";
            free(ptr);
            return -1;
        }

        free(ptr);
    }
    ////////////////////////////////////////////////////////////////////////////
    // realpath
    {
        const char* path = "/tmp";
        char resolved_path[PATH_MAX];

        if (realpath(path, resolved_path) == nullptr) 
        {
            std::cout << "Failure realpath\n";
            return -1;
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    // strdup
    {
        const char* str = "Hello, World!";
        auto duplicated = strdup(str);

        if (duplicated == nullptr || std::strcmp(duplicated, str) != 0) 
        {
            std::cout << "Failure strdup\n";
            free(duplicated);
            return -1;
        }

        free(duplicated);
    }
    ////////////////////////////////////////////////////////////////////////////
    // strndup
    {
        const char* str = "Hello, World!";
        auto duplicated = strndup(str, 5); // Only duplicate the first 5 characters

        if (duplicated == nullptr || std::strncmp(duplicated, "Hello", 5) != 0) 
        {
            std::cout << "Failure strndup\n";
            free(duplicated);
            return -1;
        }

        free(duplicated);
    }
    ////////////////////////////////////////////////////////////////////////////
    std::cout << "All good\n";
    return 0;
}

inline bool validate_buffer(void* buffer, std::size_t buffer_size)
{
    char* char_buffer = static_cast<char*>(buffer);

    // TRY WRITING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        char* dest = char_buffer + i;
        *dest = static_cast<char>(i);
    }

    // NOW CHECK READING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        auto test = char_buffer[i];
        if (test != static_cast<char>(i))
        {
            return false;
        }
    }

    return true;
}

inline bool is_address_aligned(void* address, std::size_t alignment)
{
    auto address_in_question = reinterpret_cast<uint64_t>( address );
    auto remainder = address_in_question - (address_in_question / alignment) * alignment;
    return remainder == 0;
}