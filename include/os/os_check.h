#pragma once

//////////////////////////////////////////////////////////////////////
// OPERATING SYSTEM CHECK
#if (! defined(__linux__)) && (! defined(_WIN32) )
#error "This library is supported for Linux and Windows systems"
#endif