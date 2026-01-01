// ANSI coloured output for Linux , message box for Windows
#pragma once

#ifndef NDEBUG // VOLTRON_EXCLUDE
#include <cstdlib>
#include <cassert>
#ifdef __linux__ // VOLTRON_EXCLUDE
#include <cstdio>
#elif _WIN32 // VOLTRON_EXCLUDE
#include <windows.h>
#endif // VOLTRON_EXCLUDE
#endif // VOLTRON_EXCLUDE

#ifdef NDEBUG
#define llmalloc_assert_msg(expr, message) ((void)0)
#else
#ifdef __linux__
#define MAKE_RED(x)    "\033[0;31m" x "\033[0m"
#define MAKE_YELLOW(x) "\033[0;33m" x "\033[0m"
#define llmalloc_assert_msg(expr, message) \
            do { \
                if (!(expr)) { \
                    fprintf(stderr,  MAKE_RED("Assertion failed : ")  MAKE_YELLOW("%s") "\n", message); \
                    assert(false); \
                } \
            } while (0)
#elif _WIN32
#pragma comment(lib, "user32.lib")
#define llmalloc_assert_msg(expr, message) \
            do { \
                if (!(expr)) { \
                    MessageBoxA(NULL, message, "Assertion Failed", MB_ICONERROR | MB_OK); \
                    assert(false); \
                } \
            } while (0)
#endif
#endif