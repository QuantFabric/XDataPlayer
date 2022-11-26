#include "PlayerEngine.h"

extern Utils::Logger *gLogger;

PlayerEngine::PlayerEngine()
{
    m_HPPackClient = NULL;
}

void PlayerEngine::LoadTCPClient()
{
    Utils::gLogger->Log->info("PlayerEngine::LoadTCPClient start");
    m_HPPackClient = new HPPackClient(m_XDataPlayerConfig.ServerIP.c_str(), m_XDataPlayerConfig.Port);
    m_HPPackClient->Start();
}

void PlayerEngine::ReloadTCPClient()
{
    m_HPPackClient->Start();
    usleep(1000);
}

void PlayerEngine::LoadConfig(const char* yml)
{
    std::string errorBuffer;
    if(Utils::LoadXDataPlayerConfig(yml, m_XDataPlayerConfig, errorBuffer))
    {
        Utils::gLogger->Log->info("PlayerEngine::LoadXDataPlayerConfig {} successed", yml);
    }
    else
    {
        Utils::gLogger->Log->error("PlayerEngine::LoadXDataPlayerConfig {} failed, {}", yml, errorBuffer.c_str());
    }
    if(Utils::LoadTickerList(m_XDataPlayerConfig.TickerListPath.c_str(), m_TickerPropertyVec, errorBuffer))
    {
        Utils::gLogger->Log->info("PlayerEngine::LoadTickerList {} successed", m_XDataPlayerConfig.TickerListPath);
        for(auto it = m_TickerPropertyVec.begin(); it != m_TickerPropertyVec.end(); ++it)
        {
            m_TickerIndexMap[it->Ticker] = it->Index;
        }
    }
    else
    {
        Utils::gLogger->Log->error("PlayerEngine::LoadTickerList {} failed, {}", m_XDataPlayerConfig.TickerListPath, errorBuffer);
    }
}

void PlayerEngine::Start()
{
    if(!m_XDataPlayerConfig.BackTest)
    {
        LoadTCPClient();
    }
    m_pWorkThread = new std::thread(&PlayerEngine::Run, this);
    m_pWorkThread->join();
}

void PlayerEngine::Run()
{
    bool ok = Utils::ThreadBind(pthread_self(), m_XDataPlayerConfig.CPUID);
    Utils::gLogger->Log->info("PlayerEngine::Run start, Thread bind to CPUID:{} {}", m_XDataPlayerConfig.CPUID, ok);
    if(!m_XDataPlayerConfig.BackTest)
    {
        Utils::IPCMarketQueue<MarketData::TFutureMarketDataSet> MarketQueue(m_XDataPlayerConfig.TotalTick, m_XDataPlayerConfig.MarketChannelKey);
        while(true)
        {
            memset(&m_MarketDataMessage, 0, sizeof(m_MarketDataMessage));
            bool ret = m_HPPackClient->m_MarketDataMessageQueue.Pop(m_MarketDataMessage);
            if(ret)
            {
                auto it_ticker = m_TickerIndexMap.find(m_MarketDataMessage.FutureMarketData.Ticker);
                if(it_ticker != m_TickerIndexMap.end())
                {
                    Utils::gLogger->Log->debug("PlayerEngine Last Tick:{} Ticker:{} UpdateTime:{}", 
                                m_MarketDataMessage.FutureMarketData.Tick,  
                                m_MarketDataMessage.FutureMarketData.Ticker, 
                                m_MarketDataMessage.FutureMarketData.UpdateTime);
                    m_LastMarketDataMap[m_MarketDataMessage.FutureMarketData.Ticker] = m_MarketDataMessage.FutureMarketData;
                    if(m_MarketDataMessage.FutureMarketData.IsLast)
                    {
                        MarketData::TFutureMarketDataSet dataset;
                        UpdateLastMarketDataSet(dataset);
                        MarketQueue.Write(dataset.Tick, dataset);
                        Utils::gLogger->Log->info("PlayerEngine Last Tick:{} UpdateTime:{}", dataset.Tick, dataset.UpdateTime);
                    }
                }
            }
            // reload PackClient when timeout greater equals to 5s
            static unsigned long prevtimestamp = Utils::getTimeMs();
            unsigned long currenttimestamp = Utils::getTimeMs();
            if(currenttimestamp - prevtimestamp >=  5 * 1000)
            {
                m_HPPackClient->Start();
                prevtimestamp = currenttimestamp;
            }
        }
    }
    else
    {
        Utils::gLogger->Log->info("PlayerEngine::Run BackTest Mode, Start Read Market Data and Replay Market Data");
        long start = Utils::getTimeMs();
        std::vector<std::string> fileVector;
        if(Utils::ReadDirectory(m_XDataPlayerConfig.MarketDataPath, fileVector))
        {
            Utils::gLogger->Log->info("PlayerEngine::Run ReadDirectory successed, {}", m_XDataPlayerConfig.MarketDataPath);
            std::vector<std::string> MatchedFileVec;
            bool begin = false;
            for(int j = 0; j < fileVector.size(); j++)
            {
                std::string FilePath = fileVector.at(j);
                if(FilePath.find(m_XDataPlayerConfig.BeginDay) != std::string::npos)
                {
                    begin = true;
                }
                if(begin)
                {
                    MatchedFileVec.push_back(FilePath);
                }
                else
                {
                    continue;
                }
                if(FilePath.find(m_XDataPlayerConfig.EndDay) != std::string::npos)
                {
                    break;
                }
            }
            for(int i = 0; i < MatchedFileVec.size(); i++)
            {
                std::string FilePath = m_XDataPlayerConfig.MarketDataPath + "/" + MatchedFileVec.at(i);
                std::vector<std::string> lines;
                if(Utils::ReadFile(FilePath.c_str(), lines))
                {
                    if(lines.size() < 5)
                    {
                        continue;
                    }
                    Utils::IPCMarketQueue<MarketData::TFutureMarketDataSet> MarketQueue(m_XDataPlayerConfig.TotalTick, m_XDataPlayerConfig.MarketChannelKey);
                    long begin = Utils::getTimeMs();
                    int n = 0;
                    for(int j = 1; j < lines.size(); j++)
                    {
                        n += 1;
                        ParseMarketData(lines.at(j), m_MarketDataMessage.FutureMarketData);
                        m_LastMarketDataMap[m_MarketDataMessage.FutureMarketData.Ticker] = m_MarketDataMessage.FutureMarketData;
                        if(m_TickerIndexMap.size() == n)
                        {
                            MarketData::TFutureMarketDataSet dataset;
                            UpdateLastMarketDataSet(dataset);
                            MarketQueue.Write(dataset.Tick, dataset);
                            Utils::gLogger->Log->info("PlayerEngine Last Tick:{} UpdateTime:{}", dataset.Tick, dataset.UpdateTime);
                            usleep(m_XDataPlayerConfig.IntervalMS * 1000);
                            n = 0;
                        }
                    }
                    long end = Utils::getTimeMs();
                    Utils::gLogger->Log->info("PlayerEngine::Run ReadFile {} done, elapsed:{}s", FilePath, (end - begin) / 1000.0);
                }
                usleep(5*1000*1000);
            }
        }
        else
        {
            Utils::gLogger->Log->warn("PlayerEngine::Run ReadDirectory failed, {}", m_XDataPlayerConfig.MarketDataPath);
        }
        long stop = Utils::getTimeMs();
        Utils::gLogger->Log->info("PlayerEngine::Run Market Data Replay elapsed:{}s", (stop - start) / 1000.0);
    }
}

void PlayerEngine::UpdateLastMarketDataSet(MarketData::TFutureMarketDataSet& dataset)
{
    int Tick = -1;
    for(auto it = m_LastMarketDataMap.begin(); it != m_LastMarketDataMap.end(); it++)
    {
        if(Tick < it->second.LastTick)
        {
            Tick = it->second.LastTick;
        }
        std::string Ticker = it->first;
        int TickerIndex = m_TickerIndexMap[Ticker];
        if(TickerIndex < TICKER_COUNT)
        {
            memcpy(&dataset.MarketData[TickerIndex], &it->second, sizeof(dataset.MarketData[TickerIndex]));
        }
    }
    dataset.Tick = Tick;
    strncpy(dataset.UpdateTime, Utils::getCurrentTimeUs(), sizeof(dataset.UpdateTime));
}

void PlayerEngine::ParseMarketData(const std::string& line, MarketData::TFutureMarketData& data)
{
    memset(&data, 0, sizeof(data));
    std::vector<std::string> fileds;
    Utils::Split(line, ",", fileds);
    std::string filed;
    // LastTick,Ticker,ExchangeID,RevDataLocalTime,Tick,SectionFirstTick,SectionLastTick,TotalTick,UpdateTime,MillSec,LastPrice
    filed = fileds.at(0);
    data.LastTick = atoi(filed.c_str());
    filed = fileds.at(1);
    strncpy(data.Ticker, filed.c_str(), sizeof(data.Ticker));
    filed = fileds.at(2);
    strncpy(data.ExchangeID, filed.c_str(), sizeof(data.ExchangeID));
    filed = fileds.at(3);
    strncpy(data.RevDataLocalTime, filed.c_str(), sizeof(data.RevDataLocalTime));
    filed = fileds.at(4);
    data.Tick = atoi(filed.c_str());
    filed = fileds.at(5);
    data.SectionFirstTick = atoi(filed.c_str());
    filed = fileds.at(6);
    data.SectionLastTick = atoi(filed.c_str());
    filed = fileds.at(7);
    data.TotalTick = atoi(filed.c_str());
    filed = fileds.at(8);
    strncpy(data.UpdateTime, filed.c_str(), sizeof(data.UpdateTime));
    filed = fileds.at(9);
    data.MillSec = atoi(filed.c_str());
    filed = fileds.at(10);
    data.LastPrice = atof(filed.c_str());

    // PreSettlementPrice,PreClosePrice,Volume,Turnover,OpenInterest,OpenPrice,HighestPrice,LowestPrice,UpperLimitPrice,LowerLimitPrice
    filed = fileds.at(11);
    data.PreSettlementPrice = atof(filed.c_str());
    filed = fileds.at(12);
    data.PreClosePrice = atof(filed.c_str());
    filed = fileds.at(13);
    data.Volume = atoi(filed.c_str());
    filed = fileds.at(14);
    data.Turnover = atof(filed.c_str());
    filed = fileds.at(15);
    data.OpenInterest = atof(filed.c_str());
    filed = fileds.at(16);
    data.OpenPrice = atof(filed.c_str());
    filed = fileds.at(17);
    data.HighestPrice = atof(filed.c_str());
    filed = fileds.at(18);
    data.LowestPrice = atof(filed.c_str());
    filed = fileds.at(19);
    data.UpperLimitPrice = atof(filed.c_str());
    filed = fileds.at(20);
    data.LowerLimitPrice = atof(filed.c_str());

    // BidPrice1,BidVolume1,AskPrice1,AskVolume1,BidPrice2,BidVolume2,AskPrice2,AskVolume2
    filed = fileds.at(21);
    data.BidPrice1 = atof(filed.c_str());
    filed = fileds.at(22);
    data.BidVolume1 = atoi(filed.c_str());
    filed = fileds.at(23);
    data.AskPrice1 = atof(filed.c_str());
    filed = fileds.at(24);
    data.AskVolume1 = atoi(filed.c_str());
    filed = fileds.at(25);
    data.BidPrice2 = atof(filed.c_str());
    filed = fileds.at(26);
    data.BidVolume2 = atoi(filed.c_str());
    filed = fileds.at(27);
    data.AskPrice2 = atof(filed.c_str());
    filed = fileds.at(28);
    data.AskVolume2 = atoi(filed.c_str());
    // BidPrice3,BidVolume3,AskPrice3,AskVolume3,BidPrice4,BidVolume4,AskPrice4,AskVolume4
    filed = fileds.at(29);
    data.BidPrice3 = atof(filed.c_str());
    filed = fileds.at(30);
    data.BidVolume3 = atoi(filed.c_str());
    filed = fileds.at(31);
    data.AskPrice3 = atof(filed.c_str());
    filed = fileds.at(32);
    data.AskVolume3 = atoi(filed.c_str());
    filed = fileds.at(33);
    data.BidPrice4 = atof(filed.c_str());
    filed = fileds.at(34);
    data.BidVolume4 = atoi(filed.c_str());
    filed = fileds.at(35);
    data.AskPrice4 = atof(filed.c_str());
    filed = fileds.at(36);
    data.AskVolume4 = atoi(filed.c_str());
    // BidPrice5,BidVolume5,AskPrice5,AskVolume5,ErrorID,IsLast
    filed = fileds.at(37);
    data.BidPrice5 = atof(filed.c_str());
    filed = fileds.at(38);
    data.BidVolume5 = atoi(filed.c_str());
    filed = fileds.at(39);
    data.AskPrice5 = atof(filed.c_str());
    filed = fileds.at(40);
    data.AskVolume5 = atoi(filed.c_str());
    filed = fileds.at(41);
    data.ErrorID = atoi(filed.c_str());
    filed = fileds.at(42);
    if(filed == "true")
    {
        data.IsLast = true;
    }
    else
    {
        data.IsLast = false;
    }
}