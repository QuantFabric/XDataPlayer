#ifndef PLAYERENGINE_H
#define PLAYERENGINE_H

#include <list>
#include <vector>
#include <mutex>
#include <unordered_map>
#include "HPPackClient.h"
#include "YMLConfig.hpp"
#include "IPCMarketQueue.hpp"
#include "Util.hpp"
#include "MarketData.hpp"

class PlayerEngine
{
public:
    explicit PlayerEngine();
    void LoadConfig(const char* yml);
    void Start();
protected:
    void LoadTCPClient();
    void ReloadTCPClient();
    void Run();
    void UpdateLastMarketDataSet(MarketData::TFutureMarketDataSet& dataset);
    void ParseMarketData(const std::string& line, MarketData::TFutureMarketData& data);
private:
    HPPackClient* m_HPPackClient;
    Utils::XDataPlayerConfig m_XDataPlayerConfig;
    std::thread* m_pWorkThread;
    Message::PackMessage m_MarketDataMessage;
    std::unordered_map<std::string, MarketData::TFutureMarketData> m_LastMarketDataMap;
    std::vector<Utils::TickerProperty> m_TickerPropertyVec;
    std::unordered_map<std::string, int> m_TickerIndexMap;
};

#endif // PLAYERENGINE_H