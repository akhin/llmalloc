#pragma once

#include <cstddef>
#include <cctype>
#include <cstdlib>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#ifdef DISPLAY_ENV_VARS
#include <cstdio>
#define MAKE_RED(x)    "\033[0;31m" x "\033[0m"
#define MAKE_BLUE(x)   "\033[0;34m" x "\033[0m"
#define MAKE_YELLOW(x) "\033[0;33m" x "\033[0m"
#endif

class EnvironmentVariable
{
    public:

        // Does not allocate memory
        template <typename T>
        static T get_variable(const char* environment_variable_name, T default_value)
        {
            T value = default_value;
            char* str_value = nullptr;

            #ifdef _WIN32
            // MSVC does not allow std::getenv due to safety
            std::size_t str_value_len;
            errno_t err = _dupenv_s(&str_value, &str_value_len, environment_variable_name);
            if (err)
                return value;
            #elif __linux__
            str_value = std::getenv(environment_variable_name);
            #endif

            if (str_value)
            {
                if constexpr (std::is_arithmetic<T>::value)
                {
                    char* end_ptr = nullptr;
                    auto current_val = std::strtold(str_value, &end_ptr);

                    if (*end_ptr == '\0')
                    {
                        value = static_cast<T>(current_val);
                    }
                }
                else if constexpr (std::is_same<T, char*>::value || std::is_same<T, const char*>::value)
                {
                    value = str_value;
                }
            }

            #ifdef DISPLAY_ENV_VARS
            // Non allocating trace
            fprintf(stderr, MAKE_RED("variable:") " " MAKE_BLUE("%s") ", " MAKE_RED("value:") "  ", environment_variable_name);
            if constexpr (std::is_same<T, double>::value) 
            {
                fprintf(stderr, MAKE_YELLOW("%f") "\n", value);
            }
            else if constexpr (std::is_same<T, std::size_t>::value)
            {
                fprintf(stderr, MAKE_YELLOW("%zu") "\n", value);
            }
            else if constexpr (std::is_arithmetic<T>::value)
            {
                fprintf(stderr, MAKE_YELLOW("%d") "\n", value);
            }
            else
            {
                fprintf(stderr, MAKE_YELLOW("%s") "\n", value);
            }
            #endif

            return value;
        }

        // Utility function when handling csv numeric parameters from environment variables, does not allocate memory
        static void set_numeric_array_from_comma_separated_value_string(std::size_t* target_array, std::size_t array_size, const char* str)
        {
            auto len = strlen(str);

            constexpr std::size_t MAX_STRING_LEN = 64;
            constexpr std::size_t MAX_TOKEN_LEN = 8;
            std::size_t start = 0;
            std::size_t end = 0;
            std::size_t counter = 0;
            
            auto is_string_numeric = [](const char*str)
            {
                auto len = std::strlen(str);
                for(std::size_t i = 0 ; i<len; i++)
                {
                    if (!std::isdigit(static_cast<unsigned char>(str[i]))) 
                    {
                        return false;
                    }
                }
                
                return true;
            };

            while (end <= len && end < MAX_STRING_LEN - 1 && counter <array_size)
            {
                if (str[end] == ',' || (end > start && end == len))
                {
                    char token[MAX_TOKEN_LEN];
                    std::size_t token_len = end - start;

                    #ifdef __linux__
                    strncpy(token, str + start, token_len);
                    #elif _WIN32
                    strncpy_s(token, str + start, token_len);
                    #endif
                    token[token_len] = '\0';
                    
                    if(is_string_numeric(token) == false)
                    {
                        return;
                    }

                    target_array[counter] = atoi(token);

                    start = end + 1;
                    counter++;
                }

                ++end;
            }
        }
};