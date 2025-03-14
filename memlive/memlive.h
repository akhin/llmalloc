/*
MEMLIVE 1.0.0

MIT License

Copyright (c) 2025 Akin Ocal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef _MEMLIVE_H_
#define _MEMLIVE_H_

// STD C
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <malloc.h>
#include <new>
// CPU INTRINSICS
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__)
#include <emmintrin.h>
#endif
// STD
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <iomanip>
#include <memory>
#include <type_traits>
#include <mutex> // For std::lock_guard
#ifdef __linux__
#include <string.h>
#include <csignal>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#endif
#ifdef _WIN32
#include <Ws2tcpip.h>
// WINDOWS , WE WANT IT AFTER LIVE PROFILER INCLUDES AS WINDOWS.H CAN'T BE INCLUDED BEFORE Ws2tcpip.h ( ALTERNATIVE WAY -> do "#define WIN32_LEAN_AND_MEAN" )
#include <windows.h>
#include <chrono>
#endif

namespace memlive
{
//////////////////////////////////////////////////////////////////////
// COMPILER CHECK
#if (! defined(_MSC_VER)) && (! defined(__GNUC__))
#error "This library is supported for only GCC and MSVC compilers"
#endif

//////////////////////////////////////////////////////////////////////
// C++ VERSION CHECK
#if defined(_MSC_VER)
#if _MSVC_LANG < 201703L
#error "This library requires to be compiled with C++17"
#endif

#elif defined(__GNUC__)
#if __cplusplus < 201703L
#error "This library requires to be compiled with C++17"
#endif

#endif

//////////////////////////////////////////////////////////////////////
// OPERATING SYSTEM CHECK

#if (! defined(__linux__)) && (! defined(_WIN32) )
#error "This library is supported for Linux and Windows systems"
#endif

//////////////////////////////////////////////////////////////////////
// Compare and swap, standard C++ provides them however it requires non-POD std::atomic usage
// They are needed when we want to embed spinlocks in "packed" data structures which need all members to be POD such as headers
#if defined(__GNUC__)
#define builtin_cas(pointer, old_value, new_value) __sync_val_compare_and_swap(pointer, old_value, new_value)
#elif defined(_MSC_VER)
#define builtin_cas(pointer, old_value, new_value) _InterlockedCompareExchange(reinterpret_cast<long*>(pointer), new_value, old_value)
#endif

//////////////////////////////////////////////////////////////////////
// memcpy
#if defined(__GNUC__)
#define builtin_memcpy(destination, source, size)     __builtin_memcpy(destination, source, size)
#elif defined(_MSC_VER)
#define builtin_memcpy(destination, source, size)     std::memcpy(destination, source, size)
#endif

//////////////////////////////////////////////////////////////////////
// memset
#if defined(__GNUC__)
#define builtin_memset(destination, character, count)  __builtin_memset(destination, character, count)
#elif defined(_MSC_VER)
#define builtin_memset(destination, character, count)  std::memset(destination, character, count)
#endif

//////////////////////////////////////////////////////////////////////
// aligned_alloc , It exists because MSVC does not provide std::aligned_alloc
#if defined(__GNUC__)
#define builtin_aligned_alloc(size, alignment)  std::aligned_alloc(alignment, size)
#define builtin_aligned_free(ptr)               std::free(ptr)
#elif defined(_MSC_VER)
#define builtin_aligned_alloc(size, alignment)  _aligned_malloc(size, alignment)
#define builtin_aligned_free(ptr)               _aligned_free(ptr)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline))
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALIGN_DATA , some GCC versions gives warnings about standard C++ 'alignas' when applied to data
#ifdef __GNUC__
#define ALIGN_DATA( _alignment_ ) __attribute__((aligned( (_alignment_) )))
#elif _MSC_VER
#define ALIGN_DATA( _alignment_ ) alignas( _alignment_ )
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UNUSED
//To avoid unused variable warnings
#if defined(__GNUC__)
#define UNUSED(x) (void)(x)
#elif defined(_MSC_VER)
#define UNUSED(x) __pragma(warning(suppress:4100)) x
#endif

/*
    Intel initially advised using _mm_pause in spin-wait loops in case of hyperthreading
    Before Skylake it was about 10 cycles, but with Skylake it becomes 140 cycles and that applies to successor architectures
    -> Intel opt manual 2.5.4 "Pause Latency in Skylake Client Microarchitecture"

    Later _tpause  / _umonitor / _umwait instructions were introduced however not using them for the time being as they are not widespread yet

    Pause implementation is instead using nop
*/

inline void pause(uint16_t repeat_count=100)
{
    #if defined(__GNUC__)
    // rep is for repeating by the no provided in 16 bit cx register
    __asm__ __volatile__("mov %0, %%cx\n\trep; nop" : : "r" (repeat_count) : "cx");
    #elif defined(_WIN32)
    for (uint16_t i = 0; i < repeat_count; ++i)
    {
        _mm_lfence();
        __nop();
        _mm_lfence();
    }
    #endif

}

#ifdef __linux__
#elif _WIN32
#pragma comment(lib,"Ws2_32.lib")
#pragma warning(disable:4996)
#endif

enum class SocketType
{
    TCP,
    UDP
};

enum class SocketOptionLevel
{
    SOCKET,
    TCP,
    IP
};

enum class SocketOption
{
    GET_ERROR_AND_CLEAR,
    REUSE_ADDRESS,
    REUSE_PORT,
    KEEP_ALIVE,
    EXCLUSIVE_ADDRESS,
    RECEIVE_BUFFER_SIZE,
    RECEIVE_BUFFER_TIMEOUT,
    SEND_BUFFER_SIZE,
    SEND_BUFFER_TIMEOUT,
    TCP_ENABLE_CORK,
    TCP_ENABLE_QUICKACK,        // Applies only to Linux , even Nagle is turned off , delayed can cause time loss due in case of lost packages
    TCP_DISABLE_NAGLE,          // Send packets as soon as possible , no need to wait for ACKs or to reach a certain amount of buffer size
    POLLING_INTERVAL,           // SO_BUSY_POLL , specifies time to wait for async io to query kernel to know if new data received
    SOCKET_PRIORITY,
    TIME_TO_LIVE,
    ZERO_COPY,                  // https://www.kernel.org/doc/html/v4.15/networking/msg_zerocopy.html
};

enum class SocketState
{
    DISCONNECTED,
    BOUND,
    CONNECTED,
    LISTENING,
    ACCEPTED
};

class SocketAddress
{
    public:

        void initialise(const std::string_view& address, int port)
        {
            m_port = port;
            m_address = address;

            auto addr = &m_socket_address_struct;
            
            builtin_memset(addr, 0, sizeof(sockaddr_in));
            addr->sin_family = PF_INET;
            addr->sin_port = htons(port);

            if (m_address.size() > 0)
            {
                if (get_address_info(m_address.c_str(), &(addr->sin_addr)) != 0)
                {
                    inet_pton(PF_INET, m_address.c_str(), &(addr->sin_addr));
                }
            }
            else
            {
                addr->sin_addr.s_addr = INADDR_ANY;
            }
        }

        void initialise(struct sockaddr_in* socket_address_struct)
        {
            char ip[64];
            #ifdef __linux__
            inet_ntop(PF_INET, (struct in_addr*)&(socket_address_struct->sin_addr.s_addr), ip, sizeof(ip) - 1);
            #elif _WIN32
            InetNtopA(PF_INET, (struct in_addr*)&(socket_address_struct->sin_addr.s_addr), ip, sizeof(ip) - 1);
            #endif

            m_address = ip;
            m_port = ntohs(socket_address_struct->sin_port);
        }

        const std::string& get_address() const
        {
            return m_address;
        }

        int get_port() const
        {
            return m_port;
        }

        struct sockaddr_in* get_socket_address_struct()
        {
            return &m_socket_address_struct;
        }

    private:
        int m_port = 0;
        std::string m_address;
        struct sockaddr_in m_socket_address_struct;

        static int get_address_info(const char* hostname, struct in_addr* socket_address)
        {
            struct addrinfo *res{ nullptr };

            int result = getaddrinfo(hostname, nullptr, nullptr, &res);

            if (result == 0)
            {
                builtin_memcpy(socket_address, &((struct sockaddr_in *) res->ai_addr)->sin_addr, sizeof(struct in_addr));
                freeaddrinfo(res);
            }

            return result;
        }
};

template <SocketType socket_type = SocketType::TCP>
class Socket
{
    public :

        // 2 static methods needs to be called at the beginning and end of a program
        static void socket_library_initialise()
        {
            #ifdef _WIN32
            WORD version = MAKEWORD(2, 2);
            WSADATA data;
            WSAStartup(version, &data);
            #endif

        }

        static void socket_library_uninitialise()
        {
            #ifdef _WIN32
            WSACleanup();
            #endif

        }

        Socket():m_socket_descriptor{0}, m_state{ SocketState::DISCONNECTED }, m_pending_connections_queue_size{0}
        {
            socket_library_initialise();
        }

        ~Socket()
        {
            close();
            socket_library_uninitialise();
        }

        bool create()
        {
            if constexpr(socket_type == SocketType::TCP)
            {
                m_socket_descriptor = static_cast<int>(socket(PF_INET, SOCK_STREAM, 0));
            }
            else if constexpr (socket_type == SocketType::UDP)
            {
                m_socket_descriptor = static_cast<int>(socket(PF_INET, SOCK_DGRAM, 0));
            }

            if (m_socket_descriptor < 0)
            {
                return false;
            }

            return true;
        }

        void close()
        {            
            if (m_socket_descriptor > 0)
            {
                #ifdef __linux__
                ::close(m_socket_descriptor);
                #elif _WIN32
                ::closesocket(m_socket_descriptor);
                #endif
                
                m_socket_descriptor = 0;
            }
            m_state = SocketState::DISCONNECTED;
        }
        
        /*
            BY DEFAULT , LINUX APPS GET SIGPIPE SIGNALS WHEN THEY WRITE TO WRITE/SEND 
            ON CLOSED SOCKETS WHICH MAKES IT IMPOSSIBLE TO DETECT CONNECTION LOSS DURING SENDS. 
            BY IGNORING THE SIGNAL , CONNECTION LOSS DETECTION CAN BE DONE INSIDE THE CALLER APP
            
            NOTE THAT CALL TO THIS ONE WILL AFFECT THE ENTIRE APPLICATION
        */
        void ignore_sigpipe_signals()
        {
            #ifdef __linux__
            signal(SIGPIPE, SIG_IGN);
            #endif

        }

        // For acceptors :  it is listening address and port
        // For connectors : it is the NIC that the application wants to use for outgoing connection. 
        //                  in connector case port can be specified as 0
        bool bind(const std::string_view& address, int port)
        {
            m_bind_address.initialise(address, port);

            int result = ::bind(m_socket_descriptor, (struct sockaddr*)m_bind_address.get_socket_address_struct(), sizeof(struct sockaddr_in));

            if (result != 0)
            {
                return false;
            }

            m_state = SocketState::BOUND;

            return true;
        }
        
        // UDP functionality
        bool join_multicast_group(const std::string_view& multicast_address)
        {
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr(multicast_address.data());
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            
            #ifdef __linux__
            if (setsockopt(m_socket_descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            #elif _WIN32
            if (setsockopt(m_socket_descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) < 0)
            #endif

            {
                return false;
            }
            
            return true;
        }

        // UDP functionality
        bool leave_multicast_group(const std::string_view& multicast_address)
        {
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr(multicast_address.data());
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);

            #ifdef __linux__
            if (setsockopt(m_socket_descriptor, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            #elif _WIN32
            if (setsockopt(m_socket_descriptor, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) < 0)
            #endif

            {
                return false;
            }

            return true;
        }

        bool connect(const std::string_view& address, int port)
        {
            set_endpoint(address, port);

            auto ret = ::connect(m_socket_descriptor, (struct sockaddr*)m_endpoint_address.get_socket_address_struct(), sizeof(struct sockaddr_in));

            if (ret  != 0)
            {
                return false;
            }

            m_state = SocketState::CONNECTED;
            return true;
        }

        Socket* accept(int timeout_seconds)
        {
            if (m_state != SocketState::LISTENING && m_state != SocketState::ACCEPTED)
            {
                return nullptr;
            }

            auto blocking_mode_before_the_call = is_in_blocking_mode();
            set_blocking_mode(false);

            bool success{ true };
            ///////////////////////////////////////////////////
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(m_socket_descriptor, &read_set);
                
            struct timeval tv;
            tv.tv_sec = timeout_seconds;
            tv.tv_usec = 0;

            success = (::select(m_socket_descriptor + 1, &read_set, nullptr, nullptr, &tv) <= 0) ? false : true;
            ///////////////////////////////////////////////////
            int connector_socket_desc{ -1 };
            
            struct sockaddr_in address;
            socklen_t len = sizeof(address);
            builtin_memset(&address, 0, sizeof(address));
            
            if (success)
            {
                connector_socket_desc = static_cast<int>( ::accept(m_socket_descriptor, (struct sockaddr*)&address, &len) );

                if (connector_socket_desc < 0)
                {
                    success = false;
                }
            }

            set_blocking_mode(blocking_mode_before_the_call);

            if (!success)
            {
                return nullptr;
            }

            // It is caller`s responsibility to delete it
            Socket* connector_socket{ nullptr };
            connector_socket = new Socket;
            connector_socket->initialise(connector_socket_desc, &address);
            connector_socket->m_state = SocketState::CONNECTED;

            m_state = SocketState::ACCEPTED;

            return connector_socket;
        }

        void set_pending_connections_queue_size(int value)
        {
            m_pending_connections_queue_size = value;
        }

        void set_blocking_mode(bool blocking_mode)
        {
            #if __linux__
            long arg = fcntl(m_socket_descriptor, F_GETFL, NULL);

            if (blocking_mode)
            {
                arg &= (~O_NONBLOCK);
            }
            else
            {
                arg |= O_NONBLOCK;
            }

            fcntl(m_socket_descriptor, F_SETFL, arg);
            #elif _WIN32
            u_long mode{ 0 };

            if (!blocking_mode)
            {
                mode = 1;
            }

            ioctlsocket(m_socket_descriptor, FIONBIO, &mode);
            #endif

            m_in_blocking_mode = blocking_mode;
        }

        bool set_socket_option(SocketOptionLevel level, SocketOption option, int value)
        {
            int actual_option = get_socket_option_value(option);

            if (actual_option == -1)
            {
                // Even though called ,not supported on this system, for ex QUICK_ACK for Windows
                return false;
            }

            int actual_level = get_socket_option_level_value(level);

            if (actual_level == -1)
            {
                return false;
            }

            int actual_value = value;
            int ret = -1;
            #if __linux
            ret = setsockopt(m_socket_descriptor, actual_level, actual_option, &actual_value, sizeof(actual_value));
            #elif _WIN32
            ret = setsockopt(m_socket_descriptor, actual_level, actual_option, (char*)&actual_value, sizeof(actual_value));
            #endif

            return (ret == 0) ? true : false;
        }

        bool set_socket_option(SocketOptionLevel level, SocketOption option, const char* buffer, std::size_t buffer_len)
        {
            int actual_option = get_socket_option_value(option);

            if (!actual_option)
            {
                // Even though called ,not supported on this system, for ex QUICK_ACK for Windows
                return false;
            }

            int actual_level = get_socket_option_level_value(level);

            if (actual_level == -1)
            {
                return false;
            }

            int ret = setsockopt(m_socket_descriptor, actual_level, actual_option, buffer, sizeof buffer_len);
            return (ret == 0) ? true : false;
        }

        bool listen()
        {
            int result = ::listen(m_socket_descriptor, m_pending_connections_queue_size);

            if (result != 0)
            {
                return false;
            }

            m_state = SocketState::LISTENING;
            return true;
        }

        int get_socket_option(SocketOptionLevel level, SocketOption option)
        {
            int actual_level = get_socket_option_level_value(level);
            int actual_option = get_socket_option_value(option);

            int ret{ 0 };
            socklen_t len = sizeof(ret);

            #ifdef __linux__
            getsockopt(m_socket_descriptor, actual_level, actual_option, (void*)(&ret), &len);
            #elif _WIN32
            getsockopt(m_socket_descriptor, actual_level, actual_option, (char*)(&ret), &len);
            #endif

            return ret;
        }

        void get_socket_option(SocketOptionLevel level, SocketOption option, char* buffer, std::size_t buffer_len)
        {
            int actual_level = get_socket_option_level_value(level);
            int actual_option = get_socket_option_value(option);
            getsockopt(m_socket_descriptor, actual_level, actual_option, buffer, buffer_len);
        }

        static int get_current_thread_last_socket_error()
        {
            int ret{ -1 };
            #ifdef __linux__
            ret = errno;
            #elif _WIN32
            ret = WSAGetLastError();
            #endif

            return ret;
        }

        static std::string get_socket_error_as_string(int error_code)
        {
            std::string ret;
            #ifdef __linux__
            ret = strerror(error_code);
            #elif _WIN32
            HMODULE lib = ::LoadLibraryA("WSock32.dll");
            char* temp_string = nullptr;

            FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                (LPCVOID)lib, error_code,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&temp_string, 0, NULL);

            if (temp_string)
            {
                ret = temp_string;
                LocalFree(temp_string);
            }

            if (lib)
            {
                ::FreeLibrary(lib);
            }
            #endif

            return ret;
        }

        SocketState get_state() const
        {
            return m_state;
        }

        int get_socket_descriptor() const
        {
            return m_socket_descriptor;
        }

        int receive(char* buffer, std::size_t len)
        {
            return ::recv(m_socket_descriptor, buffer, static_cast<int>(len), static_cast<int>(0));
        }
        
        // UDP functionality
        int receive_from(char* buffer, std::size_t len)
        {
            return ::recvfrom(m_socket_descriptor, buffer, static_cast<int>(len), 0, nullptr, nullptr);
        }

        int send(const char* buffer, std::size_t len)
        {
            return ::send(m_socket_descriptor, buffer, static_cast<int>(len), static_cast<int>(0)) ;
        }

        int send_zero_copy(const char* buffer, std::size_t len)
        {
            #ifdef MSG_ZEROCOPY
            return ::send(m_socket_descriptor, buffer, static_cast<int>(len), MSG_ZEROCOPY);
            #else
            return ::send(m_socket_descriptor, buffer, static_cast<int>(len), static_cast<int>(0));
            #endif

        }
        
        // UDP functionality
        int send_to(const char* buffer, std::size_t len)
        {
            return ::sendto(m_socket_descriptor, buffer, static_cast<int>(len), 0, (struct sockaddr*)m_endpoint_address.get_socket_address_struct(), sizeof(struct sockaddr_in)) ;
        }

        void set_endpoint(const std::string_view& address , int port)
        {
            m_endpoint_address.initialise(address, port);
        }

        bool is_in_blocking_mode() const { return m_in_blocking_mode; }

        /*
            Note for non-blocking/asycn-io sockets : recv will result with return code 0
            therefore you also need to check recv result
        */
        bool is_connection_lost_during_receive(int error_code)
        {
            bool ret{ false };

            #ifdef __linux__
            switch (error_code)
            {
                case ECONNRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case ECONNREFUSED:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case ENOTCONN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                default:
                    break;
            }
            #elif _WIN32
            switch (error_code)
            {
                case WSAENOTCONN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAENETRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAESHUTDOWN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAECONNABORTED:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAECONNRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                default:
                    break;
            }
            #endif

            return ret;
        }

        bool is_connection_lost_during_send(int error_code)
        {
            bool ret{ false };

            #ifdef __linux__
            switch (error_code)
            {
                case ECONNRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case ENOTCONN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case EPIPE:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                default:
                    break;
            }
            #elif _WIN32
            switch (error_code)
            {
                case WSAENOTCONN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAENETRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAESHUTDOWN:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAECONNABORTED:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                case WSAECONNRESET:
                    ret = true;
                    m_state = SocketState::DISCONNECTED;
                    break;
                default:
                    break;
            }
            #endif

            return ret;
        }

private:
    int m_socket_descriptor;
    SocketState m_state;
    bool m_in_blocking_mode = true;
    int m_pending_connections_queue_size;

    SocketAddress m_bind_address;        // TCP Acceptors -> listening address  , TCP Connectors -> NIC address , UDP Multicast listeners -> NIC address
    SocketAddress m_endpoint_address;    // Used by only TCP connectors & UDP multicast publishers

    // Move ctor deletion
    Socket(Socket&& other) = delete;
    // Move assignment operator deletion
    Socket& operator=(Socket&& other) = delete;

    void initialise(int socket_descriptor, struct sockaddr_in* socket_address)
    {
        m_socket_descriptor = socket_descriptor;
        m_endpoint_address.initialise(socket_address);
    }

    int get_socket_option_level_value(SocketOptionLevel level)
    {
        int ret{ -1 };

        switch (level)
        {
            case SocketOptionLevel::SOCKET:
                ret = SOL_SOCKET;
                break;
            case SocketOptionLevel::TCP:
                ret = IPPROTO_TCP;
                break;
            case SocketOptionLevel::IP:
                ret = IPPROTO_IP;
                break;
            default:
                break;
        }

        return ret;
    }

    int get_socket_option_value(SocketOption option)
    {
        int ret{ -1 };

        switch (option)
        {
            case SocketOption::GET_ERROR_AND_CLEAR:
                ret = SO_ERROR;
                break;
            case SocketOption::REUSE_ADDRESS:
                ret = SO_REUSEADDR;
                break;
            case SocketOption::KEEP_ALIVE:
                ret = SO_KEEPALIVE;
                break;
            case SocketOption::EXCLUSIVE_ADDRESS:
                ret = SO_REUSEADDR;
                break;
            case SocketOption::REUSE_PORT:
                #ifdef SO_REUSEPORT
                ret = SO_REUSEPORT;
                #endif

                break;
            case SocketOption::RECEIVE_BUFFER_SIZE:
                ret = SO_RCVBUF;
                break;
            case SocketOption::RECEIVE_BUFFER_TIMEOUT:
                ret = SO_RCVTIMEO;
                break;
            case SocketOption::SEND_BUFFER_SIZE:
                ret = SO_SNDBUF;
                break;
            case SocketOption::SEND_BUFFER_TIMEOUT:
                ret = SO_SNDTIMEO;
                break;
            case SocketOption::TCP_DISABLE_NAGLE:
                ret = TCP_NODELAY;
                break;
            case SocketOption::TCP_ENABLE_QUICKACK:
                #ifdef TCP_QUICKACK
                ret = TCP_QUICKACK;
                #endif

                break;
            case SocketOption::TCP_ENABLE_CORK:
                #ifdef TCP_CORK
                ret = TCP_CORK;
                #endif

                break;
            case SocketOption::SOCKET_PRIORITY:
                #ifdef SO_PRIORITY
                ret = SO_PRIORITY;
                #endif

                break;
            case SocketOption::POLLING_INTERVAL:
                #ifdef SO_BUSY_POLL
                ret = SO_BUSY_POLL;
                #endif

                break;
            case SocketOption::TIME_TO_LIVE:
                #ifdef IP_TTL
                ret = IP_TTL;
                #endif

                break;
            case SocketOption::ZERO_COPY:
                #ifdef SO_ZEROCOPY
                ret = SO_ZEROCOPY;
                #endif

                break;
            default:
                break;
        }

        return ret;
    }
};

/*
    Uses ( level triggered ) epoll on Linux and select on Windows
*/
static constexpr std::size_t DEFAULT_EPOLL_MAX_DESCRIPTOR_COUNT = 64;

#ifdef __linux__

template<std::size_t MAX_DESCRIPTOR_COUNT = DEFAULT_EPOLL_MAX_DESCRIPTOR_COUNT>
class Epoll
{
public:
    Epoll()
    {
        m_max_epoll_events = MAX_DESCRIPTOR_COUNT;
        m_epoll_descriptor = epoll_create1(0);
        m_epoll_events = new struct epoll_event[m_max_epoll_events];
    }

    ~Epoll()
    {
        if (m_epoll_descriptor >= 0)
        {
            ::close(m_epoll_descriptor);
        }

        if (m_epoll_events)
        {
            delete[] m_epoll_events;
            m_epoll_events = nullptr;
        }
    }

    static constexpr bool polls_per_socket()
    {
        return false;
    }

    void set_timeout(long nanoseconds)
    {
        m_epoll_timeout_milliseconds = nanoseconds / 1000000;
        
        if( m_epoll_timeout_milliseconds == 0 )
        {
            m_epoll_timeout_milliseconds = 1;
        }
    }

    void clear_descriptors()
    {
        if (m_epoll_descriptor >= 0)
        {
            ::close(m_epoll_descriptor);
        }

        m_epoll_descriptor = epoll_create1(0);
    }

    void add_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;

        epoll_descriptor.events = EPOLLIN;

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_ADD, fd, &epoll_descriptor);
    }

    void remove_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;
        epoll_descriptor.events = EPOLLIN;

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_DEL, fd, &epoll_descriptor);
    }

    int get_number_of_ready_events()
    {
        int result{ -1 };

        result = ::epoll_wait(m_epoll_descriptor, m_epoll_events, m_max_epoll_events, m_epoll_timeout_milliseconds);

        return result;
    }

    bool is_valid_event(int index)
    {
        if (m_epoll_events[index].events & EPOLLIN)
        {
            return true;
        }

        return false;
    }

    int get_ready_descriptor(int index)
    {
        int ret{ -1 };
        ret = m_epoll_events[index].data.fd;
        return ret;
    }

    //////////////////////////////////////////////////////////////////////////////
    // COMMON INTERFACE AS GCC'S SUPPORT FOR IF-CONSTEXPR IS NOT AS GOOD AS MSVC
    // EVEN THOUGH THEY WON'T BE CALLED GCC STILL WANTS TO SEE THEM
    int get_number_of_ready_descriptors() { assert(1==0);return 0;}
    bool is_descriptor_ready(int fd) { assert(1==0); return false;}
    //////////////////////////////////////////////////////////////////////////////

private:
    int m_epoll_descriptor = -1;
    struct epoll_event* m_epoll_events = nullptr;
    
    std::size_t m_max_epoll_events = -1;

    int m_epoll_timeout_milliseconds = 0;
};

#elif _WIN32

template<std::size_t MAX_DESCRIPTOR_COUNT = 0>  // Currently not used in select, just here to conform asynciopoller interface
class Epoll
{
    public:

        Epoll()
        {
            FD_ZERO(&m_query_set);
            FD_ZERO(&m_result_set);
        }

        static constexpr bool polls_per_socket()
        {
            return true;
        }

        void clear_descriptors()
        {
            FD_ZERO(&m_query_set);
        }

        void set_timeout(long nanoseconds)
        {
            m_timeout.tv_sec = 0;
            m_timeout.tv_usec = nanoseconds / 1000;
            
            if( m_timeout.tv_usec == 0 )
            {
                m_timeout.tv_usec = 1;
            }
        }

        void add_descriptor(int fd)
        {
            if (fd > m_max_descriptor)
            {
                m_max_descriptor = fd;
            }

            FD_SET(fd, &m_query_set);
        }

        void remove_descriptor(int fd)
        {
            if (FD_ISSET(fd, &m_query_set))
            {
                FD_CLR(fd, &m_query_set);
            }
        }

        int get_number_of_ready_descriptors()
        {
            m_result_set = m_query_set;
            return ::select(m_max_descriptor + 1, &m_result_set, nullptr, nullptr, &m_timeout);
        }

        bool is_descriptor_ready(int fd)
        {
            bool ret{ false };

            ret = (FD_ISSET(fd, &m_result_set)) ? true : false;

            return ret;
        }

    private :
        int m_max_descriptor = -1;
        struct timeval m_timeout;
        fd_set m_query_set;
        fd_set m_result_set;
};

#endif

class ThreadUtilities
{
    public:

        static inline void yield()
        {
            #ifdef __linux__
            sched_yield();
            #elif _WIN32
            SwitchToThread();
            #endif
        }
        
        static inline void sleep_in_nanoseconds(unsigned long nanoseconds)
        {
            #ifdef __linux__
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = nanoseconds;
            nanosleep(&ts, nullptr);
            #elif _WIN32
            std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
            #endif
        }
};

/*  
    Standard C++ thread_local keyword does not allow you to specify thread specific destructors
    and also can't be applied to class members
*/
class ThreadLocalStorage
{
    public:

        static ThreadLocalStorage& get_instance()
        {
            static ThreadLocalStorage instance;
            return instance;
        }

        // Call it only once for a process
        bool create(void(*thread_destructor)(void*) = nullptr)
        {
            #if __linux__
            return pthread_key_create(&m_tls_index, thread_destructor) == 0;
            #elif _WIN32
            // Using FLSs rather TLS as it is identical + TLSAlloc doesn't support dtor
            m_tls_index = FlsAlloc(thread_destructor);
            return m_tls_index == FLS_OUT_OF_INDEXES ? false : true;
            #endif

        }

        // Same as create
        void destroy()
        {
            if (m_tls_index)
            {
                #if __linux__
                pthread_key_delete(m_tls_index);
                #elif _WIN32
                FlsFree(m_tls_index);
                #endif

                m_tls_index = 0;
            }
        }

        // GUARANTEED TO BE THREAD-SAFE/LOCAL
        void* get()
        {
            #if __linux__
            return pthread_getspecific(m_tls_index);
            #elif _WIN32
            return FlsGetValue(m_tls_index);
            #endif

        }

        void set(void* data_address)
        {
            #if __linux__
            pthread_setspecific(m_tls_index, data_address);
            #elif _WIN32
            FlsSetValue(m_tls_index, data_address);
            #endif

        }

        // DOES NOT INVOKE SYSCALLS , JUST ACCESSES SEGMENT REGISTERS
        // SO IDEAL FOR IDENTIFIYING THREADS THAT USE TLS
        static inline uint64_t get_thread_local_storage_id(void)
        {
            uint64_t result;
            #ifdef _WIN32
            // GS
            result = reinterpret_cast<uint64_t>(NtCurrentTeb());
            #elif __linux__
            // FS
            asm volatile("mov %%fs:0, %0" : "=r"(result));
            #endif

            return result;
        }

    private:
            #if __linux__
            pthread_key_t m_tls_index = 0;
            #elif _WIN32
            unsigned long m_tls_index = 0;
            #endif

            ThreadLocalStorage() = default;
            ~ThreadLocalStorage() = default;

            ThreadLocalStorage(const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage& operator= (const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage(ThreadLocalStorage&& other) = delete;
            ThreadLocalStorage& operator=(ThreadLocalStorage&& other) = delete;
};

class Pow2Utilities
{
    public:

        // Reference : https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        static std::size_t get_first_pow2_of(std::size_t input)
        {
            if (input <= 1)
            {
                return 1;
            }

            input--;
            input |= input >> 1;
            input |= input >> 2;
            input |= input >> 4;
            input |= input >> 8;
            input |= input >> 16;

            return input + 1;
        }

        static std::size_t log2(std::size_t n)
        {
            std::size_t result = 0;
            while (n >>= 1)
            {
                ++result;
            }
            return result;
        }
};

/*
    A CAS ( compare-and-swap ) based POD ( https://en.cppreference.com/w/cpp/language/classes#POD_class ) spinlock
    As it is POD , it can be used inside packed declarations.

    To keep it as POD :
                1. Not using standard C++ std::atomic
                2. Member variables should be public

    Otherwise GCC will generate : warning: ignoring packed attribute because of unpacked non-POD field

    PAHOLE OUTPUT :

                size: 4, cachelines: 1, members: 1
                last cacheline: 4 bytes

    Can be faster than os/lock or std::mutex
    However should be picked carefully as it will affect all processes on a CPU core
    even though they are not doing the same computation so misuse may lead to starvation for others

    Doesn`t check against uniprocessors. Prefer "Lock" in os/lock.h for old systems
*/

// Pass alignment = AlignmentConstants::CACHE_LINE_SIZE to make the lock cacheline aligned

template<std::size_t alignment=sizeof(uint32_t), std::size_t spin_count = 1024, std::size_t pause_count = 64, bool extra_system_friendly = false>
struct UserspaceSpinlock
{
    // No privates, ctors or dtors to stay as PACKED+POD
    ALIGN_DATA(alignment) uint32_t m_flag=0;

    void initialise()
    {
        m_flag = 0;
    }

    void lock()
    {
        while (true)
        {
            for (std::size_t i(0); i < spin_count; i++)
            {
                if (try_lock() == true)
                {
                    return;
                }

                pause(pause_count);
            }

            if constexpr (extra_system_friendly)
            {
                ThreadUtilities::yield();
            }
        }
    }

    FORCE_INLINE bool try_lock()
    {
        if (builtin_cas(&m_flag, 0, 1) == 1)
        {
            return false;
        }

        return true;
    }

    FORCE_INLINE void unlock()
    {
        m_flag = 0;
    }
};

/*
    Using epoll on Linux and select on Windows as their latest ioring and iouring are not widespread available
*/
template <typename SocketType>
class Connectors
{
    public:

        Connectors()
        {
            m_connector_sockets.reserve(DEFAULT_ASYNC_IO_MAX_DESCRIPTOR_COUNT);
            m_connector_sockets_connection_flags.reserve(DEFAULT_ASYNC_IO_MAX_DESCRIPTOR_COUNT);
            m_connector_socket_index_table.reserve(DEFAULT_ASYNC_IO_MAX_DESCRIPTOR_COUNT);
        }

        std::size_t get_capacity() const
        {
            return m_connector_sockets.size();
        }

        int get_socket_descriptor(std::size_t index)
        {
            return m_connector_sockets[index]->get_socket_descriptor();
        }

        std::size_t add_connector(SocketType* connector)
        {
            std::size_t current_size = m_connector_sockets.size();
            int non_used_connector_index = -1;
            std::size_t ret = -1;

            for (std::size_t i{ 0 }; i < current_size; i++)
            {
                if (m_connector_sockets_connection_flags[i] == false)
                {
                    non_used_connector_index = static_cast<int>(i);
                    break;
                }
            }

            if (non_used_connector_index == -1)
            {
                // No empty slot , create new
                m_connector_sockets_connection_flags.push_back(true);
                m_connector_sockets.emplace_back(connector);
                ret = m_connector_sockets.size() - 1;
            }
            else
            {
                // Use an existing connector slot
                m_connector_sockets[non_used_connector_index].reset(connector);
                m_connector_sockets_connection_flags[non_used_connector_index] = true;
                ret = non_used_connector_index;
            }

            auto desc = connector->get_socket_descriptor();
            m_connector_socket_index_table[desc] = ret;

            return ret;
        }

        void remove_connector(std::size_t connector_index)
        {
            m_connector_sockets_connection_flags[connector_index] = false;
            auto connector_socket = get_connector_socket(connector_index);
            connector_socket->close();
        }

        std::size_t get_connector_index_from_descriptor(int fd)
        {
            return m_connector_socket_index_table[fd];
        }

        void close_all_sockets()
        {
            for (auto& connector_socket : m_connector_sockets)
            {
                connector_socket->close();
            }
        }

        SocketType* get_connector_socket(std::size_t connector_index)
        {
            return m_connector_sockets[connector_index].get();
        }

private:
    static constexpr std::size_t DEFAULT_ASYNC_IO_MAX_DESCRIPTOR_COUNT = 64;
    std::vector<std::unique_ptr<SocketType>> m_connector_sockets;
    std::vector<bool> m_connector_sockets_connection_flags;
    std::unordered_map<int, std::size_t> m_connector_socket_index_table;
};

template <typename TcpReactorImplementation, typename AsyncIOPollerType>
class TcpReactor
{
    public:

        ~TcpReactor()
        {
            stop();
        }

        bool start(const std::string& address, int port, int poll_timeout_nanoseconds = TcpReactor::DEFAULT_POLL_TIMEOUT_NANOSECONDS , int accept_timeout_seconds = TcpReactor::DEFAULT_ACCEPT_TIMEOUT_SECONDS, int pending_connection_queue_size = TcpReactor::DEFAULT_PENDING_CONNECTION_QUEUE_SIZE)
        {
            m_is_stopping.store(false);
            m_accept_timeout_seconds = accept_timeout_seconds;

            m_acceptor_socket.create();
            
            m_acceptor_socket.ignore_sigpipe_signals();
            
            m_acceptor_socket.set_pending_connections_queue_size(pending_connection_queue_size);

            m_acceptor_socket.set_socket_option(SocketOptionLevel::SOCKET, SocketOption::REUSE_ADDRESS, 1);

            if (!m_acceptor_socket.bind(address, port))
            {
                return false;
            }

            if (!m_acceptor_socket.listen())
            {
                return false;
            }

            m_acceptor_socket.set_blocking_mode(false);

            m_asio_reader.set_timeout(poll_timeout_nanoseconds);
            m_asio_reader.add_descriptor(m_acceptor_socket.get_socket_descriptor());

            m_reactor_thread.reset( new std::thread(&TcpReactor::reactor_thread, this) );

            return true;
        }

        void stop()
        {
            if (m_is_stopping.load() == false)
            {
                m_is_stopping.store(true);

                std::lock_guard<UserspaceSpinlock<>> lock(m_lock);

                m_asio_reader.clear_descriptors();

                if (m_reactor_thread.get() != nullptr)
                {
                    m_reactor_thread->join();
                }

                m_connectors.close_all_sockets();
            }
        }

        void reactor_thread()
        {
            while (true)
            {
                if (m_is_stopping.load() == true)
                {
                    break;
                }

                std::lock_guard<UserspaceSpinlock<>> lock(m_lock);

                int result = 0;

                if constexpr(AsyncIOPollerType::polls_per_socket() == false)
                {
                    result = m_asio_reader.get_number_of_ready_events();
                }
                else
                {
                    result = m_asio_reader.get_number_of_ready_descriptors();
                }

                if (result > 0)
                {
                    if constexpr (AsyncIOPollerType::polls_per_socket() == false)
                    {
                        for (int counter{ 0 }; counter < result; counter++)
                        {
                            auto current_descriptor = m_asio_reader.get_ready_descriptor(counter);
                            size_t peer_index = m_connectors.get_connector_index_from_descriptor(current_descriptor);

                            if (m_asio_reader.is_valid_event(counter))
                            {
                                if (current_descriptor == m_acceptor_socket.get_socket_descriptor())
                                {
                                    accept_new_connection();
                                }
                                else
                                {
                                    on_data_ready(peer_index);
                                }
                            }
                        }
                    }
                    else
                    {
                        if (m_asio_reader.is_descriptor_ready(m_acceptor_socket.get_socket_descriptor()))
                        {
                            accept_new_connection();
                        }

                        auto peer_count = m_connectors.get_capacity();
                        for (int counter{ 0 }; counter < static_cast<int>(peer_count); counter++)
                        {
                            if (m_asio_reader.is_descriptor_ready(m_connectors.get_socket_descriptor(counter)))
                            {
                                on_data_ready(counter);
                            }
                        }
                    }
                }
                else if (result != 0)  // 0 means timeout
                {
                    auto error_code = Socket<>::get_current_thread_last_socket_error();
                    on_async_io_error(error_code, result);
                }
            }
        }

        std::size_t accept_new_connection()
        {
            std::size_t connector_index{ 0 };

            Socket<SocketType::TCP>* connector_socket = nullptr;
            connector_socket = m_acceptor_socket.accept(m_accept_timeout_seconds);

            if (connector_socket)
            {
                connector_index = m_connectors.add_connector(connector_socket);
                auto desc = connector_socket->get_socket_descriptor();
                m_asio_reader.add_descriptor(desc);
                on_client_connected(connector_index);
            }

            return connector_index;
        }

        Socket<SocketType::TCP>& get_acceptor_socket()
        {
            return m_acceptor_socket;
        }

        constexpr static int inline DEFAULT_POLL_TIMEOUT_NANOSECONDS = 5;
        constexpr static int inline DEFAULT_ACCEPT_TIMEOUT_SECONDS = 5;
        constexpr static int inline DEFAULT_PENDING_CONNECTION_QUEUE_SIZE = 32;

        void on_client_disconnected(std::size_t connector_index)
        {
            m_asio_reader.remove_descriptor(m_connectors.get_socket_descriptor(connector_index));
            m_connectors.remove_connector(connector_index);
            // on_client_disconnected is supposed to be invoked always from derived classes
            // therefore it is up to the derived class to call this method explicitly
        }

        ///////////////////////////////////////////////////////////////////////////////////
        // CRTP STATICALLY-POLYMORPHIC METHODS
        void on_client_connected(std::size_t connector_index)
        {
            derived_class_implementation().on_client_connected(connector_index);
        }

        void on_async_io_error(int error_code, int event_result)
        {
            derived_class_implementation().on_async_io_error(error_code, event_result);
        }

        void on_data_ready(std::size_t connector_index)
        {
            derived_class_implementation().on_data_ready(connector_index);
        }
        ///////////////////////////////////////////////////////////////////////////////////

    private:
        std::unique_ptr<std::thread> m_reactor_thread;
        int m_accept_timeout_seconds = -1;
        Socket<SocketType::TCP> m_acceptor_socket;
        AsyncIOPollerType m_asio_reader;
        std::atomic<bool> m_is_stopping = false;
        UserspaceSpinlock<> m_lock;
        TcpReactorImplementation& derived_class_implementation() { return *static_cast<TcpReactorImplementation*>(this); }

    protected:
        Connectors<Socket<SocketType::TCP>> m_connectors;
};

/* 
    CPR : Connection per request

    ITS PARSER ONLY HANDLES CONNECTION-PER-REQUEST MODEL WHERE THERE WILL BE ONE UNIQUE TCP CONNECTION PER HTTP REQUEST
    THEREFORE THE PARSER DOES NOT CONSIDER INCOMPLETE BYTES 
    
    USE CASE :   IN SIMPLE HTTP APPS AJAX'S XMLHttpRequest FOR POST REQUEST , YOU CAN NOT REUSE AN EXISTING TCP CONNECTION
                 Https://stackoverflow.com/questions/32505128/how-to-make-xmlhttprequest-reuse-a-tcp-connection
*/
namespace HTTPConstants
{
    static inline const char* VERB_POST = "POST";
    static inline const char* VERB_GET = "GET";
    static inline const char* VERB_PUT = "PUT";
    static inline const char* VERB_DEL = "DEL";
}

enum class HTTPVerb
{
    NONE, // PARSE ERRORS OR UNHANDLED HTTP VERBS
    POST, // CREATE
    GET,  // READ
    PUT,  // UPDATE
    DEL   // DELETE
};

class HttpRequest
{
    public:

        void set_verb(HTTPVerb verb) { m_verb = verb; }
        void set_body(const std::string_view& body) { m_body = body; }

        const std::string body() const { return m_body; }
        const HTTPVerb verb() const { return m_verb; }
    private:
        HTTPVerb m_verb = HTTPVerb::NONE;
        std::string m_body;
};

class HttpResponse
{
    public:
        void set_response_code_with_text(std::string code_with_text) { m_response_code_with_text = code_with_text; }
        void set_body(const std::string_view& body) { m_body = body; }
        void set_connection_alive(bool b) { m_connection_alive = b; }
        void set_content_type(const std::string_view& content_type) { m_content_type = content_type; }
        void set_http_version(const std::string_view& version) { m_http_version = version; }

        const std::string get_as_text()
        {
            std::string ret;
            ret += "HTTP/" + m_http_version + " ";
            ret += m_response_code_with_text;
            ret += "\nConnection: " + (m_connection_alive ? std::string("keep-alive") : std::string("Closed"));
            ret += "\nContent-Type: " + m_content_type;
            ret += "\nContent length: " + std::to_string(m_body.size());
            ret += "\n\n";
            ret += m_body;
            return ret;
        }

    private:
        std::string m_response_code_with_text = "";
        bool m_connection_alive = true;
        std::string m_http_version = "1.1";
        std::string m_content_type = "text/html";
        std::string m_body = "";
};

template <typename HTTPCPRReactorImplementation>
class HTTPCPRReactor : public  TcpReactor<HTTPCPRReactor<HTTPCPRReactorImplementation>, Epoll<>>
{
public:

    HTTPCPRReactor()
    {
        m_cache.reserve(RECEIVE_SIZE);
    }

    ~HTTPCPRReactor() {}
    HTTPCPRReactor(const HTTPCPRReactor& other) = delete;
    HTTPCPRReactor& operator= (const HTTPCPRReactor& other) = delete;
    HTTPCPRReactor(HTTPCPRReactor&& other) = delete;
    HTTPCPRReactor& operator=(HTTPCPRReactor&& other) = delete;

    void on_data_ready(std::size_t peer_index)
    {
        auto peer_socket = this->m_connectors.get_connector_socket(peer_index);
        std::size_t received_bytes{ 0 };

        char read_buffer[RECEIVE_SIZE] = { 0 };
        auto read = peer_socket->receive(read_buffer, RECEIVE_SIZE);

        if (read > 0 && read <= static_cast<int>(RECEIVE_SIZE))
        {
            received_bytes += read;
            read_buffer[read] = '\0';
            m_cache += read_buffer;
        }
        else 
        {
            auto error = Socket<>::get_current_thread_last_socket_error();
            
            if( read == 0)
            {
                on_client_disconnected(peer_index);
            }
            else if (peer_socket->is_connection_lost_during_receive(error))
            {
                on_client_disconnected(peer_index);
            }           
            else if (error != 0)
            {
                this->on_socket_error(error, read);
            }
        }

        if (received_bytes > 0)
        {
            std::array<HttpRequest, MAX_INCOMING_HTTP_REQUEST_COUNT> http_requests;
            std::size_t http_request_count = 0;

            if (parse_http_request(http_requests, http_request_count))
            {
                for(std::size_t i =0; i< http_request_count; i++)
                {
                    switch (http_requests[i].verb())
                    {
                    case HTTPVerb::NONE:
                        assert(0 == 1); // INVALID BUFFER
                        break;
                    case HTTPVerb::GET:
                        on_http_get_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::PUT:
                        on_http_put_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::POST:
                        on_http_post_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::DEL:
                        on_http_delete_request(http_requests[i], peer_socket);
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        on_client_disconnected(peer_index);
    }

    void on_http_get_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_get_request(http_request, connector_socket);
    }

    void on_http_post_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_post_request(http_request, connector_socket);
    }

    void on_http_put_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_put_request(http_request, connector_socket);
    }

    void on_http_delete_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_delete_request(http_request, connector_socket);
    }

    void on_client_connected(std::size_t peer_index)
    {
        UNUSED(peer_index);
    }

    void on_client_disconnected(std::size_t peer_index)
    {
        TcpReactor<HTTPCPRReactor, Epoll<>>::on_client_disconnected(peer_index);
    }

    void on_async_io_error(int error_code, int event_result)
    {
        UNUSED(error_code);
        UNUSED(event_result);
    }

    void on_socket_error(int error_code, int event_result)
    {
        UNUSED(error_code);
        UNUSED(event_result);
    }

private:
    static inline constexpr std::size_t RECEIVE_SIZE = 4096;
    static inline constexpr std::size_t MAX_INCOMING_HTTP_REQUEST_COUNT = 32;
    std::string m_cache;
    std::size_t m_cache_index = 0;

private:

    /*
        Example GET request :

            GET / HTTP/1.1
            Host: localhost:555
            Connection: keep-alive
            sec-ch-ua: "Chromium";v="118", "Google Chrome";v="118", "Not=A?Brand";v="99"
            ...

        Example POST request :

            POST /your_server_url_here HTTP/1.1
            Host: localhost:555
            Connection: keep-alive
            Content-Length: 6
            sec-ch-ua: "Chromium";v="118", "Google Chrome";v="118", "Not=A?Brand";v="99"
            ...
            Accept - Language : en - GB, en - US; q = 0.9, en; q = 0.8, tr; q = 0.7

            data = A
    */
    bool parse_http_request(std::array<HttpRequest, MAX_INCOMING_HTTP_REQUEST_COUNT>& http_requests, std::size_t& http_request_count)
    {
        std::size_t num_processed_characters{ 0 };
        http_request_count = 0;

        while (true)
        {
            char* buffer_start = &m_cache[0] + m_cache_index + num_processed_characters;
            std::size_t buffer_length = m_cache.length() - m_cache_index - num_processed_characters;
            std::string_view buffer(buffer_start, buffer_length);

            if (buffer.find("Host: ") == std::string_view::npos) // It is mandatory for all HTTP versions to include 'Host' attribute
            {
                break;
            }

            HttpRequest request;

            // EXTRACT VERB
            auto verb_line_end_position = buffer.find("\r\n");

            if (verb_line_end_position == std::string_view::npos)
            {
                return false;
            }

            std::string_view verb_line(buffer.data(), verb_line_end_position);

            auto verb_line_verb_end_position = verb_line.find(' ');

            std::string_view verb(verb_line.data(), verb_line_verb_end_position);

            if (verb == HTTPConstants::VERB_GET)
            {
                request.set_verb(HTTPVerb::GET);
            }
            else if (verb == HTTPConstants::VERB_POST)
            {
                request.set_verb(HTTPVerb::POST);
            }
            else if (verb == HTTPConstants::VERB_PUT)
            {
                request.set_verb(HTTPVerb::PUT);
            }
            else if (verb == HTTPConstants::VERB_DEL)
            {
                request.set_verb(HTTPVerb::DEL);
            }

            // EXTRACT BODY
            constexpr std::size_t double_newline_length = 4;
            auto body_start_position = buffer.find("\r\n\r\n");
            std::string_view body(buffer.data() + body_start_position + double_newline_length, buffer_length - body_start_position - double_newline_length);
            request.set_body(body);

            http_requests[http_request_count] = request;
            http_request_count++;
            num_processed_characters += (body_start_position + double_newline_length);
        }

        m_cache_index += (m_cache.length() - m_cache_index);

        return true;
    }
};

/////////////////////////////////////////////////////////////
// PROFILER
#ifndef MEMLIVE_MAX_SIZE_CLASS_COUNT
#define MEMLIVE_MAX_SIZE_CLASS_COUNT 21 // UP TO 2^(21-1)/1 MB
#endif

struct SizeClassStats
{
    std::size_t m_alloc_count = 0;
    std::size_t m_free_count = 0;
    std::size_t m_peak_usage_count = 0;
    int m_usage_count = 0;

    void add_allocation()
    {
        m_usage_count++;

        if (m_usage_count > 0 && static_cast<std::size_t>(m_usage_count) > m_peak_usage_count)
        {
            m_peak_usage_count = static_cast<std::size_t>(m_usage_count);
        }
        else if(m_usage_count<0 && m_peak_usage_count == 0 )
        {
            m_peak_usage_count = 1;
        }

        m_alloc_count++;
    }

    void add_deallocation()
    {
        m_usage_count--;
        m_free_count++;
    }

    void reset()
    {
        m_alloc_count = 0;
        m_free_count = 0;
        m_peak_usage_count = 0;
        m_usage_count = 0;
    }
};

struct ThreadStats
{
    std::size_t m_tls_id = 0;
    std::array<SizeClassStats, MEMLIVE_MAX_SIZE_CLASS_COUNT> m_size_class_stats;
};

class Profiler
{
    public:

        static Profiler& get_instance()
        {
            static Profiler instance;
            return instance;
        }

        static constexpr inline std::size_t MAX_THREAD_COUNT = 64;

        void capture_allocation(void* address, std::size_t size)
        {
            UNUSED(address);

            if(size == 0 ) return ;

            auto index = find_stats_index_from_size(size);

            if (index >= MEMLIVE_MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_allocation();
        }

        void capture_deallocation(void* address)
        {
            if(address == nullptr) return;

            std::size_t original_allocation_size = 0;

            #ifdef __linux__
            original_allocation_size = static_cast<std::size_t>(malloc_usable_size(address));
            #elif _WIN32
            original_allocation_size = static_cast<std::size_t>(_msize(address));
            #endif

            auto index = find_stats_index_from_size(original_allocation_size);

            if (index >= MEMLIVE_MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_deallocation();
        }
        
        void capture_aligned_deallocation(void* address, std::size_t alignment)
        {
            if(address == nullptr) return;

            std::size_t original_allocation_size = 0;

            #ifdef __linux__
            UNUSED(alignment);
            original_allocation_size = static_cast<std::size_t>(malloc_usable_size(address));
            #elif _WIN32
            original_allocation_size = static_cast<std::size_t>(_aligned_msize(address, alignment, 0));
            #endif

            auto index = find_stats_index_from_size(original_allocation_size);

            if (index >= MEMLIVE_MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_deallocation();
        }
        
        void capture_custom_deallocation(void* address, std::size_t original_allocation_size)
        {
            if(address == nullptr) return;

            auto index = find_stats_index_from_size(original_allocation_size);

            if (index >= MEMLIVE_MAX_SIZE_CLASS_COUNT) return;

            ThreadStats* thread_local_stats = get_thread_local_stats();

            if (thread_local_stats == nullptr) return; // MAX_THREAD_COUNT was not enough

            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            (*thread_local_stats).m_size_class_stats[index].add_deallocation();
        }

        void reset_stats()
        {
            std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

            for(std::size_t i=0; i<MAX_THREAD_COUNT; i++)
            {
                for(auto stats : m_stats[i].m_size_class_stats)
                {
                    stats.reset();
                }
            }
        }

        ThreadStats* get_stats() { return &m_stats[0]; }
        std::size_t get_observed_thread_count() { return m_observed_thread_count; }

    private:
        UserspaceSpinlock<> m_lock;
        std::size_t m_observed_thread_count = 0;
        ThreadStats m_stats[MAX_THREAD_COUNT];

        Profiler()
        {
            m_lock.initialise();
            ThreadLocalStorage::get_instance().create();
        }

        ~Profiler()
        {
            ThreadLocalStorage::get_instance().destroy();
        }

        Profiler(const Profiler& other) = delete;
        Profiler& operator= (const Profiler& other) = delete;
        Profiler(Profiler&& other) = delete;
        Profiler& operator=(Profiler&& other) = delete;

        ThreadStats* get_thread_local_stats()
        {
            ThreadStats* stats = reinterpret_cast<ThreadStats*>(ThreadLocalStorage::get_instance().get());

            if (stats == nullptr)
            {
                std::lock_guard<UserspaceSpinlock<>> guard(m_lock);

                if (m_observed_thread_count != MAX_THREAD_COUNT)
                {
                    m_stats[m_observed_thread_count].m_tls_id = ThreadLocalStorage::get_thread_local_storage_id();
                    stats = &(m_stats[m_observed_thread_count]);
                    ThreadLocalStorage::get_instance().set(stats);
                    m_observed_thread_count++;
                }
            }

            return stats;
        }

        std::size_t find_stats_index_from_size(std::size_t size)
        {
            auto index = Pow2Utilities::get_first_pow2_of(size);
            index = Pow2Utilities::log2(index);
            return index;
        }
};

} // NAMESPACE END 

/////////////////////////////////////////////////////////////
// PROFILER PROXIES
inline void* proxy_malloc(std::size_t size)
{
    void* ret = malloc(size);
    memlive::Profiler::get_instance().capture_allocation(ret, size);
    return ret;
}

inline void* proxy_calloc(std::size_t count, std::size_t size)
{
    void * ret =  calloc(count, size);
    memlive::Profiler::get_instance().capture_allocation(ret, count*size);
    return ret;
}

inline void* proxy_realloc(void* address, std::size_t size)
{
    void* ret= realloc(address, size);
    memlive::Profiler::get_instance().capture_allocation(address, size);
    return ret;
}

inline void proxy_free(void* address)
{
    memlive::Profiler::get_instance().capture_deallocation(address);
    free(address);
}

inline void proxy_aligned_free(void* address, std::size_t alignment)
{
    memlive::Profiler::get_instance().capture_aligned_deallocation(address, alignment);
    builtin_aligned_free(address);
}

inline void* proxy_aligned_alloc(std::size_t size, std::size_t alignment)
{
    void * ret = builtin_aligned_alloc(size, alignment);
    memlive::Profiler::get_instance().capture_allocation(ret, size);
    return ret;
}

static memlive::UserspaceSpinlock<> g_new_handler_lock;

inline void handle_operator_new_failure()
{
    std::new_handler handler;

    // std::get_new_handler is not thread safe
    g_new_handler_lock.lock();
    handler = std::get_new_handler();
    g_new_handler_lock.unlock();

    if(handler != nullptr)
    {
        handler();
    }
    else
    {
        throw std::bad_alloc();
    }
}

inline void* proxy_operator_new(std::size_t size)
{
    void* ret = proxy_malloc(size);

    if( ret==nullptr )
    {
        handle_operator_new_failure();
    }

    return ret;
}

inline void* proxy_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    void* ret = proxy_aligned_alloc(size, alignment);

    if( ret==nullptr )
    {
        handle_operator_new_failure();
    }

    return ret;
}

/////////////////////////////////////////////////////////////
// PROFILER REDIRECTIONS
#ifndef MEMLIVE_DISABLE_REDIRECTIONS

#define malloc(size) proxy_malloc(size)
#define calloc(count, size) proxy_calloc(count, size)
#define realloc(address,size) proxy_realloc(address,size)
#define free(address) proxy_free(address)

#if __linux__
#define aligned_alloc(alignment, size) proxy_aligned_alloc(size, alignment)
#endif

#if _WIN32
#define _aligned_malloc(size, alignment) proxy_aligned_alloc(size, alignment)
#define _aligned_free(ptr) proxy_aligned_free(ptr, 16)
#endif

/////////////////////////////////////////////////////////////
// PROFILER OPERATOR NEW DELETE OVERLOADS

// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    return proxy_operator_new(size);
}

void operator delete(void* ptr)
{
    proxy_free(ptr);
}

void* operator new[](std::size_t size)
{
    return proxy_operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    proxy_free(ptr);
}

// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return proxy_malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    proxy_free(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return proxy_malloc(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    proxy_free(ptr);
}

// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return proxy_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    proxy_aligned_free(ptr, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return proxy_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    proxy_aligned_free(ptr, static_cast<std::size_t>(alignment));
}

// WITH ALIGNMENT std::size_t
void* operator new(std::size_t size, std::size_t alignment)
{
    return proxy_operator_new_aligned(size, alignment);
}

void* operator new[](std::size_t size, std::size_t alignment)
{
    return proxy_operator_new_aligned(size, alignment);
}

// WITH ALIGNMENT and std::nothrow_t
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
    return proxy_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
    return proxy_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    proxy_aligned_free(ptr, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    proxy_aligned_free(ptr, static_cast<std::size_t>(alignment));
}

// WITH ALIGNMENT and std::nothrow_t & std::size_t alignment not std::align_val_t
void* operator new(std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
    return proxy_aligned_alloc(size, alignment);
}

void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
    return proxy_aligned_alloc(size, alignment);
}

void operator delete(void* ptr, std::size_t alignment, const std::nothrow_t &) noexcept
{
    proxy_aligned_free(ptr, alignment);
}

void operator delete[](void* ptr, std::size_t alignment, const std::nothrow_t &) noexcept
{
    proxy_aligned_free(ptr, alignment);
}

// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    proxy_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    proxy_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    proxy_aligned_free(ptr, static_cast<std::size_t>(align));
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    proxy_aligned_free(ptr, static_cast<std::size_t>(align));
}

void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    proxy_aligned_free(ptr, align);
}

void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    proxy_aligned_free(ptr, align);
}
#endif

namespace memlive
{

/////////////////////////////////////////////////////////////
// MINIFIED HTML-JAVASCRIPT SOURCE

static const char* HTTPLiveProfilerHtmlSource = R"(
<!DOCTYPE html><html><head><title>memlive 1.0.0</title><style>.header {display: flex;}.header div {margin-right: 10px;}.dark-mode {background-color: #333;color: #fff;}.dark-mode th {background-color: #444;color: #fff;}#poll_interval_notification {display: none;position: fixed;bottom: 10px;left: 10px;background-color: #4CAF50;color: #fff;padding: 15px;border-radius: 5px;box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);}canvas {border: 1px solid #000;}</style></head><body><div id="poll_interval_notification"></div><div class="header"><div>   </div><label for="darkModeCheckbox" style="display: inline;">Dark Mode</label><input type="checkbox" id="darkModeCheckbox"><div>   </div><div><label for="pollingInterval">Polling interval ( millis ):</label><input type="text" id="pollingInterval" value="0" style="width: 30px;"><button id="pollingIntervalButton">Apply polling interval</button><span style="margin: 0 5px;">|</span><button id="saveToFileButton">Save data to file</button></div></div><canvas id="areaChart" width="570" height="60"></canvas><div id="parent-container" style="display: flex; flex-direction: row;"><div id="combobox-container"><label>Threads</label></div><div id="table-container" style="float: right;"></div></div><script>const darkModeCheckbox = document.getElementById("darkModeCheckbox");let darkMode = true;let uiCreated = false;let pollingTimer;let pollingIntervalMilliseconds = 1000;let csvData;let csvDataTokens;let statsNumberPerSizeClass = 3;let currentUsage = 0;let chartDataMaxWidth = 50;let chartScale = 0;let chartData = [];const chartAreaColourDarkMode = 'rgba(0, 128, 255, 0.9)';const chartAreaColourLightMode = 'rgba(0, 128, 255, 0.3)';let chartAreaColour = chartAreaColourDarkMode;const chartTextColourDarkMode = 'white';const chartTextColourLightMode = 'black';let chartTextColour = chartTextColourDarkMode;function getHumanReadibleSizeString(bytes){const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];if (bytes === 0) return '0 Byte';const i = parseInt(Math.floor(Math.log(bytes) / Math.log(1024)));return Math.round(100 * (bytes / Math.pow(1024, i))) / 100 + ' ' + sizes[i];}function postRequest(){var xhr = new XMLHttpRequest();xhr.open("POST", window.location.href, true);xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");var data = "memlive-request";xhr.onreadystatechange = function () {if (xhr.readyState === 4) {if (xhr.status === 201){csvData = xhr.responseText;csvDataTokens = csvData.split(',');if(uiCreated == false){createUI();uiCreated=true;}updateTable();updateCurrentUsage();drawChart("Current usage" + ': ' + getHumanReadibleSizeString(chartData[chartDataMaxWidth - 1]));setupPollingTimer();}}};xhr.send(data);}function setupPollingTimer(){if (pollingTimer){clearTimeout(pollingTimer);}pollingTimer = setTimeout(postRequest, pollingIntervalMilliseconds);}function set_poll_interval_notification(){const newInterval = parseInt(document.getElementById('pollingInterval').value);pollingIntervalMilliseconds = newInterval;var notification = document.getElementById("poll_interval_notification");notification.innerHTML = "Successfully set the polling interval to " + parseInt(document.getElementById('pollingInterval').value).toString() + " milliseconds.";notification.style.display = "block";setTimeout(function(){notification.style.display = "none";}, 3000);}function updateChartScale(){if (chartData.length > 0){const maxDataValue = Math.max(...chartData);const powerOf10 = Math.pow(10, Math.floor(Math.log10(maxDataValue)));chartScale = Math.ceil(maxDataValue / powerOf10) * powerOf10;}else{chartScale = 100;}}function updateChartData(newValue){chartData.push(newValue);if (chartData.length > chartDataMaxWidth){chartData.shift();}updateChartScale();}function drawChart(chartTopLeftText){const canvas = document.getElementById('areaChart');const ctx = canvas.getContext('2d');const width = canvas.width;const height = canvas.height;ctx.clearRect(0, 0, width, height);ctx.strokeStyle = 'rgba(0, 0, 0, 0.8)';ctx.lineWidth = 0.5;const stepX = width / (chartDataMaxWidth - 1);const stepY = height / 5;for (let i = 0; i < chartDataMaxWidth; i++){const x = i * stepX;ctx.beginPath();ctx.moveTo(x, 0);ctx.lineTo(x, height);ctx.stroke();}for (let i = 0; i <= 5; i++){const y = i * stepY;ctx.beginPath();ctx.moveTo(0, y);ctx.lineTo(width, y);ctx.stroke();}ctx.fillStyle = chartAreaColour;ctx.beginPath();ctx.moveTo(0, height);for (let i = 0; i < chartDataMaxWidth; i++){const x = i * stepX;const y = height - (chartData[i] / chartScale) * height;ctx.lineTo(x, y);}ctx.lineTo(width, height);ctx.closePath();ctx.fill();ctx.fillStyle = chartTextColour;ctx.font = '14px Arial';const textPosX = 10;const textPosY = 20;ctx.fillText(chartTopLeftText, textPosX, textPosY);}function getThreadCount(){return csvDataTokens[0];}function getSizeClassCount(){return csvDataTokens[1];}function getAllocationCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass);return csvDataTokens[targetTokenIndex];}function getDeallocationCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass) + 1;return csvDataTokens[targetTokenIndex];}function getPeakUsageCount(selectedThreadIndex, sizeclassIndex){targetTokenIndex = 2 + (selectedThreadIndex * getSizeClassCount() * statsNumberPerSizeClass ) + (sizeclassIndex*statsNumberPerSizeClass) + 2;return csvDataTokens[targetTokenIndex];}function getTotalAllocationCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getAllocationCount(i, sizeclassIndex));}return ret;}function getTotalDeallocationCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getDeallocationCount(i, sizeclassIndex));}return ret;}function getTotalPeakUsageCountForSizeClass(sizeclassIndex){ret=0;threadCount = getThreadCount();for (let i = 0; i < threadCount; i++){ret += parseInt(getPeakUsageCount(i, sizeclassIndex));}return ret;}function getOverallPeakUsageInBytes(){ret=0;sizeclassCount = getSizeClassCount();for (let i = 0; i < sizeclassCount; i++){ret += parseInt(getTotalPeakUsageCountForSizeClass(i)) * Math.pow(2, i);}return ret;}function createUI(){threadCount = getThreadCount();const comboBox = document.createElement('select');comboBox.id = 'threadsCombobox';for (let i = 0; i < threadCount; i += 1){const option = document.createElement('option');option.text = "Thread " + (i+1).toString();comboBox.add(option);}const option = document.createElement('option');option.text = "Total";comboBox.add(option);const container = document.getElementById('combobox-container');container.appendChild(comboBox);const table = document.createElement('table');table.id = 'dataTable';table.border = '1';const tableContainer = document.getElementById('table-container');tableContainer.innerHTML = '';tableContainer.appendChild(table);comboBox.addEventListener('change', function(){updateTable();});}function updateCurrentUsage(){calculatedUsage = 0;threadCount = getThreadCount();sizeclassCount = getSizeClassCount();for (let i = 0; i < threadCount; i++){for (let j = 0; j < sizeclassCount; j++){currentAllocationCount = getAllocationCount(i, j);currentDeallocationCount = getDeallocationCount(i, j);if(currentAllocationCount>currentDeallocationCount){calculatedUsage += ((currentAllocationCount-currentDeallocationCount) * Math.pow(2, j));}}}currentUsage = calculatedUsage;updateChartData(currentUsage);}function updateCombobox(){threadCount = getThreadCount();const comboBox = document.getElementById('threadsCombobox');const previousSelectedText = comboBox.options[comboBox.selectedIndex]?.text;while (comboBox.options.length > 0){comboBox.remove(0);}for (let i = 0; i < threadCount; i += 1){const option = document.createElement('option');option.text = "Thread " + (i + 1).toString();comboBox.add(option);}const totalOption = document.createElement('option');totalOption.text = "Total";comboBox.add(totalOption);const options = Array.from(comboBox.options);const match = options.find(option => option.text === previousSelectedText);if (match){comboBox.value = match.value; }else{comboBox.selectedIndex = 0; }}function updateTable(){updateCombobox();const table = document.getElementById('dataTable');threadCount = getThreadCount();sizeclassCount = getSizeClassCount();const combobox = document.getElementById('threadsCombobox');const selectedIndex = combobox.selectedIndex;if(selectedIndex >= 0 ){table.innerHTML = '';const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);sizeClassCell.textContent = 'Size Class';allocationCountCell.textContent = 'Allocation Count';deallocationCountCell.textContent = 'Deallocation Count';peakUsageCell.textContent = 'Peak Usage in bytes';for (let i = 0; i < sizeclassCount; i++){const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);const sizeClass = Math.pow(2, i);allocationCount = 0;deallocationCount = 0;peakUsageCount = 0;if(selectedIndex < threadCount ){allocationCount = getAllocationCount(selectedIndex, i);deallocationCount = getDeallocationCount(selectedIndex, i);peakUsageCount = getPeakUsageCount(selectedIndex, i);}else {allocationCount = getTotalAllocationCountForSizeClass(i);deallocationCount = getTotalDeallocationCountForSizeClass(i);peakUsageCount = getTotalPeakUsageCountForSizeClass(i);}sizeClassCell.textContent = getHumanReadibleSizeString(sizeClass);allocationCountCell.textContent = allocationCount;deallocationCountCell.textContent = deallocationCount;peakUsageCell.textContent = getHumanReadibleSizeString(sizeClass*peakUsageCount);}if(selectedIndex == threadCount ){const newRow = table.insertRow();const sizeClassCell = newRow.insertCell(0);const allocationCountCell = newRow.insertCell(1);const deallocationCountCell = newRow.insertCell(2);const peakUsageCell = newRow.insertCell(3);sizeClassCell.textContent = 'SUM';allocationCountCell.textContent = 'N/A';deallocationCountCell.textContent = 'N/A';peakUsageCell.textContent = getHumanReadibleSizeString(getOverallPeakUsageInBytes());}}}function tableToTabularString(){const table = document.getElementById('dataTable');let csvContent = '';const columnWidths = [];for (let i = 0; i < table.rows.length; i++){let row = table.rows[i];for (let j = 0; j < row.cells.length; j++){let cell = row.cells[j];let cellText = cell.textContent.trim();columnWidths[j] = Math.max(columnWidths[j] || 0, cellText.length);}}for (let i = 0; i < table.rows.length; i++){let row = table.rows[i];let rowData = [];for (let j = 0; j < row.cells.length; j++){let cell = row.cells[j];let cellText = cell.textContent.trim();cellText = cellText.replace(/"/g, '""');cellText = cellText.padEnd(columnWidths[j], ' '); rowData.push(cellText);}csvContent += rowData.join(' | ') + '\n'; }return csvContent;}function save_to_file(){const content = tableToTabularString();const comboBox = document.getElementById('threadsCombobox');suggestedFileName = comboBox.options[comboBox.selectedIndex].text;suggestedFileName = suggestedFileName.replace(/\s+/g, '').toLowerCase();suggestedFileName += ".txt";let filename = prompt("Enter file name :", suggestedFileName);if (filename !== null && filename !== ""){const blob = new Blob([content], { type: 'text/csv;charset=utf-8;' });const link = document.createElement('a');const url = URL.createObjectURL(blob);link.setAttribute('href', url);link.setAttribute('download', filename); link.style.visibility = 'hidden';document.body.appendChild(link);link.click();document.body.removeChild(link);URL.revokeObjectURL(url);}else{alert("Download canceled.");}}document.getElementById('pollingInterval').value = 1000;darkModeCheckbox.addEventListener("change", function(){if (darkModeCheckbox.checked){darkMode = true;document.body.classList.add("dark-mode");chartAreaColour = chartAreaColourDarkMode;chartTextColour = chartTextColourDarkMode;}else{darkMode = false;document.body.classList.remove("dark-mode");chartAreaColour = chartAreaColourLightMode;chartTextColour = chartTextColourLightMode;}});if(darkMode){document.body.classList.add("dark-mode");darkModeCheckbox.checked = true;}else{document.body.classList.remove("dark-mode");darkModeCheckbox.checked = false;}for (let i = 0; i < chartDataMaxWidth; i++){updateChartData(0);}document.getElementById('pollingIntervalButton').addEventListener('click', set_poll_interval_notification);document.getElementById('saveToFileButton').addEventListener('click', save_to_file);setupPollingTimer();</script></body></html>
)";

/*
    UI LOOP

        1. HTTPLiveProfiler which is an HTTP reactor, serves the page
        2. Browser side sends a post request
        3. HttpLiveProfiler responds to that post request with csv data
        4. Browser side updates UI based on that CSV data
*/

/////////////////////////////////////////////////////////////
// HTTP LIVE PROFILER

class HTTPLiveProfiler : public HTTPCPRReactor<HTTPLiveProfiler>
{
    public:
        HTTPLiveProfiler() =default;
        ~HTTPLiveProfiler() = default;
        HTTPLiveProfiler(const HTTPLiveProfiler& other) = delete;
        HTTPLiveProfiler& operator= (const HTTPLiveProfiler& other) = delete;
        HTTPLiveProfiler(HTTPLiveProfiler&& other) = delete;
        HTTPLiveProfiler& operator=(HTTPLiveProfiler&& other) = delete;

        void on_http_get_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);

            HttpResponse response;
            response.set_response_code_with_text("200 OK");
            response.set_connection_alive(false);

            response.set_body(HTTPLiveProfilerHtmlSource);

            auto response_text = response.get_as_text();
            connector_socket->send(response_text.c_str(), response_text.length());
        }

        void on_http_post_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);

            HttpResponse response;
            response.set_response_code_with_text("201 Created");
            response.set_connection_alive(false);

            update_post_response();
            response.set_body(m_post_response_buffer);

            auto response_text = response.get_as_text();
            connector_socket->send(response_text.c_str(), response_text.length());
        }

        void on_http_put_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);
            UNUSED(connector_socket);
        }

        void on_http_delete_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
        {
            UNUSED(http_request);
            UNUSED(connector_socket);
        }

    private:
        static constexpr inline std::size_t MAX_POST_RESPONSE_BUFFER_SIZE = 4096;
        static constexpr inline std::size_t MAX_POST_RESPONSE_DIGITS = 16;
        char m_post_response_buffer[MAX_POST_RESPONSE_BUFFER_SIZE] = {(char)0};

        void update_post_response()
        {
            std::size_t thread_count = Profiler::get_instance().get_observed_thread_count();
            std::size_t size_class_count = MEMLIVE_MAX_SIZE_CLASS_COUNT;

            auto stats = Profiler::get_instance().get_stats();

            snprintf(m_post_response_buffer, sizeof(m_post_response_buffer), "%zd,%zd,", thread_count-1, size_class_count);  // -1 as we are excluding the reactor thread

            for (std::size_t i = 0; i < thread_count; i++)
            {
                if (stats[i].m_tls_id != ThreadLocalStorage::get_thread_local_storage_id()) // We are excluding the reactor thread
                {
                    for (std::size_t j = 0; j < size_class_count; j++)
                    {
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_alloc_count);
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_free_count);
                        snprintf(m_post_response_buffer + strlen(m_post_response_buffer), MAX_POST_RESPONSE_DIGITS, "%zd,", stats[i].m_size_class_stats[j].m_peak_usage_count);
                    }
                }
            }
        }
};

/////////////////////////////////////////////////////////////
// MEMLIVE INTERFACE
static HTTPLiveProfiler g_http_live_profiler;

inline bool start(const std::string& address, int port)
{
    return g_http_live_profiler.start(address, port); // Reactor will start its own thread
}

inline void stop()
{
    g_http_live_profiler.stop();
}

inline void reset()
{
    Profiler::get_instance().reset_stats();
}

inline void capture_custom_allocation(void* address, std::size_t size)
{
    Profiler::get_instance().capture_allocation(address, size);
}

inline void capture_custom_deallocation(void* address, std::size_t size)
{
    Profiler::get_instance().capture_custom_deallocation(address, size);
}

}
#endif