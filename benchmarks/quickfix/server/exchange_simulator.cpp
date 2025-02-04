#include "fix_exchange_simulator.h"

#include "quickfix/Log.h"
#include "quickfix/FileStore.h"
#include "quickfix/SocketAcceptor.h"

#include "quickfix/ThreadedSocketAcceptor.h"

#include <iostream>
#include <memory>

#define THREAD_PER_CLIENT

class NoLogFactory : public FIX::LogFactory
{
    public:
        virtual ~NoLogFactory()
        {

        }
        
        virtual FIX::Log* create()
        {
            return nullptr;
        }
        
        virtual FIX::Log* create(const FIX::SessionID&)
        {
            return nullptr;
        }
        
        virtual void destroy(FIX::Log*)
        {
        }
};

int main()
{
    std::string file = "exchange_simulator.cfg";

    try
    {
        FixExchangeSimulator fix_exchange_simulator;

        FIX::SessionSettings settings(file);
        FIX::FileStoreFactory store_factory(settings);
        FIX::ScreenLogFactory log_factory(settings);

        NoLogFactory no_log_factory;

        std::unique_ptr<FIX::Acceptor> acceptor;
        
        #ifdef THREAD_PER_CLIENT
        acceptor = std::unique_ptr<FIX::Acceptor>( new FIX::ThreadedSocketAcceptor(fix_exchange_simulator, store_factory, settings, no_log_factory));
        #else
        acceptor = std::unique_ptr<FIX::Acceptor>( new FIX::SocketAcceptor(fix_exchange_simulator, store_factory, settings, log_factory));
        #endif
        
        acceptor->start();

        std::cout << "Type Ctrl-C to quit" << std::endl;

        while (true)
        {
            FIX::process_sleep(1);
        }

        acceptor->stop();
    }
    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;
        return 1;
    }

    return 0;
}