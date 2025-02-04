#ifndef __FIX_EXCHANGE_SIMULATOR__
#define __FIX_EXCHANGE_SIMULATOR__

#include <atomic>
#include <cstddef>
#include <string>
#include <mutex>

#ifdef _WIN32
#include <intrin.h>
#elif __linux__
#include <x86intrin.h>
#endif

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"

#include "quickfix/fix50/ExecutionReport.h"
#include "quickfix/fix50/NewOrderSingle.h"
#include "quickfix/fix50/OrderCancelRequest.h"
#include "quickfix/fix50/OrderCancelReplaceRequest.h"

class FixExchangeSimulator : public FIX::Application, public FIX::MessageCracker
{
public:

    FixExchangeSimulator()
    {
        m_samples.reserve(1000);
    }

    void onCreate(const FIX::SessionID& sessionID)
    {
    }

    void onLogon(const FIX::SessionID& sessionID)
    {
    }

    void onLogout(const FIX::SessionID& sessionID)
    {
    }

    void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID)
    {
    }

    void toApp(FIX::Message& message, const FIX::SessionID& sessionID) EXCEPT(FIX::DoNotSend)
    {
    }

    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon)
    {
    }

    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType)
    {
        crack(message, sessionID);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // NEW ORDERS
    void onMessage(const FIX50::NewOrderSingle& message, const FIX::SessionID& sessionID)
    {
        if (m_start == 0)
        {
            m_start = __rdtscp(&m_model_specific_register_contents);
        }
        else
        {
            auto end = __rdtscp(&m_model_specific_register_contents);
            auto sample = end - m_start;
            m_lock.lock();
            m_samples.push_back(sample);
            
            if(m_samples.size() >= 250000)
            {
                std::cout << "P50 : " << get_percentile(50) << "\n";
                std::cout << "P75 : " << get_percentile(75) << "\n";
                std::cout << "P90 : " << get_percentile(90) << "\n";
                std::cout << "P95 : " << get_percentile(95) << "\n\n";
                m_samples.clear();
            }
            
            m_lock.unlock();
        }

        // EXTRACT MESSAGE FIELDS
        FIX::Symbol symbol;
        FIX::Side side;
        FIX::OrdType order_type;
        FIX::OrderQty orig_qty;
        FIX::Price price;
        FIX::ClOrdID clordid;
        FIX::Account account;

        message.get(order_type);
        message.get(symbol);
        message.get(side);
        message.get(orig_qty);
        message.get(price);
        message.get(clordid);

        FIX50::ExecutionReport exec_report = FIX50::ExecutionReport
        (FIX::OrderID(generate_order_id()),
            FIX::ExecID(generate_exec_id()),
            FIX::ExecType(FIX::ExecType_NEW),
            FIX::OrdStatus(FIX::OrdStatus_NEW),
            side,
            FIX::LeavesQty(orig_qty),
            FIX::CumQty(0));

        exec_report.set(clordid);
        exec_report.set(symbol);
        exec_report.set(orig_qty);

        FIX::Session::sendToTarget(exec_report, sessionID);
    }

private:
    int m_order_id = 0;
    std::atomic<int> m_exec_id = 0;
    std::mutex m_lock;
    
    std::vector<unsigned long long> m_samples;
    
    unsigned long long get_percentile(int percentile)
    {
        auto sample_number = m_samples.size();

        std::sort(m_samples.begin(), m_samples.end());

        std::size_t index = sample_number * percentile / 100;

        return m_samples[index];
    }

    unsigned int m_model_specific_register_contents = 0;
    unsigned long long m_start = 0;

    std::string generate_order_id()
    {
        return std::to_string(++m_order_id);
    }

    std::string generate_exec_id()
    {
        return std::to_string(++m_exec_id);
    }
};

#endif