#ifdef _MSC_VER
#pragma warning( disable : 4503 4355 4786 )
#endif

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Values.h"
#include "quickfix/Mutex.h"
#include "quickfix/config.h"
#include "quickfix/Session.h"
#include "quickfix/FileStore.h"
#include "quickfix/SocketInitiator.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/Log.h"

#include "quickfix/fix50/NewOrderSingle.h"
#include "quickfix/fix50/ExecutionReport.h"
#include "quickfix/fix50/OrderCancelRequest.h"
#include "quickfix/fix50/OrderCancelReject.h"
#include "quickfix/fix50/OrderCancelReplaceRequest.h"
#include "quickfix/fix50/MarketDataRequest.h"

#include <array>
#include <memory>
#include <queue>
#include <vector>
#include <thread>
#include <iostream>

class Application : public FIX::Application, public FIX::MessageCracker
{
public:

    void set_id(int id)
    {
        m_id = id;
    }
    
    void run()
    {
        if (m_logged_on)
        {
            try
            {

                for (std::size_t i = 0; i < 1000; i++)
                {
                    client_order_id++;

                    FIX50::NewOrderSingle newOrder(
                        FIX::ClOrdID(std::to_string(client_order_id)),
                        FIX::Side(FIX::Side_BUY),
                        FIX::TransactTime(),
                        FIX::OrdType(FIX::OrdType_LIMIT)
                    );

                    newOrder.set(FIX::Symbol("BMWG.DE"));
                    newOrder.set(FIX::OrderQty(100));
                    newOrder.set(FIX::Price(150.00));
                    newOrder.set(FIX::TimeInForce(FIX::TimeInForce_DAY));

                    FIX::Session::sendToTarget(newOrder, m_sessionID);
                }
            }
            catch (std::exception& e)
            {
                std::cout << "Message Not Sent: " << e.what();
            }
        }
    }

private:

    std::size_t client_order_id = 0;
    FIX::SessionID m_sessionID;
    bool m_logged_on = false;
    int m_id = -1;


    void onCreate(const FIX::SessionID&) {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) {}

    void onLogon(const FIX::SessionID& sessionID)
    {
        m_sessionID = sessionID;
        m_logged_on = true;
    }

    void onLogout(const FIX::SessionID& sessionID)
    {
        m_logged_on = false;
    }

    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType)
    {
        crack(message, sessionID);
    }

    void toApp(FIX::Message& message, const FIX::SessionID& sessionID) EXCEPT(FIX::DoNotSend)
    {
        try
        {
            FIX::PossDupFlag possDupFlag;
            message.getHeader().getField(possDupFlag);
            if (possDupFlag) throw FIX::DoNotSend();
        }
        catch (FIX::FieldNotFound&) 
        {
        }
    }

    void onMessage (const FIX50::ExecutionReport&, const FIX::SessionID&) {}
    void onMessage (const FIX50::OrderCancelReject&, const FIX::SessionID&) {}
};

#define CLIENT_COUNT 16

int main()
{
    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<std::unique_ptr<FIX::Initiator>> initiators;
    std::array<Application, CLIENT_COUNT> applications;

    for (std::size_t i = 0; i < CLIENT_COUNT; i++)
    {
        std::string file = "configs/client" +  std::to_string(i+1) + ".cfg";

        FIX::SessionSettings settings(file);
        FIX::FileStoreFactory storeFactory(settings);
        FIX::ScreenLogFactory logFactory(settings);
        
        applications[i].set_id(static_cast<int>(i) + 1);
        initiators.emplace_back(new FIX::SocketInitiator(applications[i], storeFactory, settings, logFactory));
    }

    for (auto& initiator : initiators)
    {
        initiator->start();
    }

    auto client_thread_function = [&](std::size_t index)
        {
            while (true)
            {
                applications[index].run();
            }
        };


    for (std::size_t i = 0; i < CLIENT_COUNT; i++)
    {
        threads.emplace_back( new std::thread(client_thread_function, i));
    }

    std::cout << "Type Ctrl-C to quit" << std::endl;

    while (true)
    {

    }

    return 0;
}