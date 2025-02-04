#ifndef _OS_CHECK_H_
#define _OS_CHECK_H_

//////////////////////////////////////////////////////////////////////
// OPERATING SYSTEM CHECK

#if (! defined(__linux__)) && (! defined(_WIN32) )
#error "This library is supported for Linux and Windows systems"
#endif

#endif