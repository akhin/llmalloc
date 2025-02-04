#define ENABLE_OVERRIDE
#include <llmalloc.h>
using namespace std;
#include <iostream>

int main()
{
    try
    {
        char* ptr = (char*)malloc(42); // llmalloc will be initialised in the 1st call
        ptr[0]='a';                    // if initialisation fails it will throw std::runtime_error
        ptr[1]='o';
        free(ptr);
    }
    catch (const std::runtime_error& e) 
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}