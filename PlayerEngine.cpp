#include "PlayerEngine.h"


PlayerEngine::PlayerEngine()
{
    m_HPPackClient = NULL;
    m_PubServer = NULL;
}

PlayerEngine::~PlayerEngine()
{
    if(NULL != m_HPPackClient)
    {
        delete m_HPPackClient;
        m_HPPackClient = NULL;
    }
    if(NULL != m_PubServer)
    {
        delete m_PubServer;
        m_PubServer = NULL;
    }
}

void PlayerEngine::LoadTCPClient()
{
    FMTLOG(fmtlog::INF, "PlayerEngine::LoadTCPClient start");
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
        FMTLOG(fmtlog::INF, "PlayerEngine::LoadXDataPlayerConfig {} successed", yml);
    }
    else
    {
        FMTLOG(fmtlog::ERR, "PlayerEngine::LoadXDataPlayerConfig {} failed, {}", yml, errorBuffer);
    }
    if(Utils::LoadTickerList(m_XDataPlayerConfig.TickerListPath.c_str(), m_TickerPropertyVec, errorBuffer))
    {
        FMTLOG(fmtlog::INF, "PlayerEngine::LoadTickerList {} successed", m_XDataPlayerConfig.TickerListPath);
        for(auto it = m_TickerPropertyVec.begin(); it != m_TickerPropertyVec.end(); ++it)
        {
            m_TickerIndexMap[it->Ticker] = it->Index;
        }
    }
    else
    {
        FMTLOG(fmtlog::ERR, "PlayerEngine::LoadTickerList {} failed, {}", m_XDataPlayerConfig.TickerListPath, errorBuffer);
    }
}

void PlayerEngine::Start()
{
    if(!m_XDataPlayerConfig.BackTest)
    {
        LoadTCPClient();
    }
    m_PubServer = new PubServer();
    m_PubServer->Start(m_XDataPlayerConfig.MarketServer);

    m_pWorkThread = new std::thread(&PlayerEngine::Run, this);

    m_PubServer->Join();
    m_pWorkThread->join();
}

void PlayerEngine::Run()
{
    FMTLOG(fmtlog::INF, "PlayerEngine::Run start");
    if(!m_XDataPlayerConfig.BackTest)
    {
        memset(&m_MarketDataMessage, 0, sizeof(m_MarketDataMessage));
        while(true)
        {
            bool ret = m_HPPackClient->m_MarketDataMessageQueue.Pop(m_MarketDataMessage);
            if(ret)
            {
                if(Message::EMessageType::EFutureMarketData == m_MarketDataMessage.MessageType)
                {
                    auto it = m_TickerIndexMap.find(m_MarketDataMessage.FutureMarketData.Ticker);
                    if(it != m_TickerIndexMap.end())
                    {
                        if(m_PubServer->Push(m_MarketDataMessage))
                        {
                            FMTLOG(fmtlog::INF, "PlayerEngine::Run Push FutureMarketData ticker:{} {}", m_MarketDataMessage.FutureMarketData.Ticker, m_MarketDataMessage.FutureMarketData.RecvLocalTime);
                        }
                        else
                        {
                            FMTLOG(fmtlog::ERR, "PlayerEngine::Run Push FutureMarketData failed");
                        }
                    }
                }
                else if(Message::EMessageType::EStockMarketData == m_MarketDataMessage.MessageType)
                {
                    auto it = m_TickerIndexMap.find(m_MarketDataMessage.StockMarketData.Ticker);
                    if(it != m_TickerIndexMap.end())
                    {
                        if(m_PubServer->Push(m_MarketDataMessage))
                        {
                            FMTLOG(fmtlog::INF, "PlayerEngine::Run Push StockMarketData tikcer:{} {}", m_MarketDataMessage.StockMarketData.Ticker, m_MarketDataMessage.StockMarketData.RecvLocalTime);
                        }
                        else
                        {
                            FMTLOG(fmtlog::ERR, "PlayerEngine::Run Push FutureMarketData failed");
                        }
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
        FMTLOG(fmtlog::INF, "PlayerEngine::Run BackTest Mode, Start Read Future Data and Replay Future Data");
        long start = Utils::getTimeMs();
        std::vector<std::string> fileVector;
        if(Utils::ReadDirectory(m_XDataPlayerConfig.MarketDataPath, fileVector))
        {
            FMTLOG(fmtlog::INF, "PlayerEngine::Run ReadDirectory successed, {}", m_XDataPlayerConfig.MarketDataPath);
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
            FMTLOG(fmtlog::INF, "PlayerEngine::Run  Matched file: {}", MatchedFileVec.size());
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
                    long begin = Utils::getTimeMs();
                    for(int j = 1; j < lines.size(); j++)
                    {
                        ParseMarketData(lines.at(j), m_MarketDataMessage.FutureMarketData);
                        if(!m_PubServer->Push(m_MarketDataMessage))
                        {
                            FMTLOG(fmtlog::ERR, "PlayerEngine::Run Push FutureMarketData failed, j={}", j);
                        }
                    }
                    long end = Utils::getTimeMs();
                    FMTLOG(fmtlog::INF, "PlayerEngine::Run ReadFile {} done, elapsed:{}s", FilePath, (end - begin) / 1000.0);
                }
                usleep(5*1000*1000);
            }
        }
        else
        {
            FMTLOG(fmtlog::WRN, "PlayerEngine::Run ReadDirectory failed, {}", m_XDataPlayerConfig.MarketDataPath);
        }
        long stop = Utils::getTimeMs();
        FMTLOG(fmtlog::INF, "PlayerEngine::Run Market Data Replay elapsed:{}s", (stop - start) / 1000.0);
    }
}

void PlayerEngine::ParseMarketData(const std::string& line, MarketData::TFutureMarketData& data)
{
    memset(&data, 0, sizeof(data));
    std::vector<std::string> fileds;
    Utils::Split(line, ",", fileds);
    std::string filed;
    // LastTick,Ticker,ExchangeID,RecvLocalTime,Tick,SectionFirstTick,SectionLastTick,TotalTick,UpdateTime,MillSec,LastPrice
    filed = fileds.at(0);
    data.LastTick = atoi(filed.c_str());
    filed = fileds.at(1);
    strncpy(data.Ticker, filed.c_str(), sizeof(data.Ticker));
    filed = fileds.at(2);
    strncpy(data.ExchangeID, filed.c_str(), sizeof(data.ExchangeID));
    filed = fileds.at(3);
    strncpy(data.RecvLocalTime, filed.c_str(), sizeof(data.RecvLocalTime));
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
    // BidPrice5,BidVolume5,AskPrice5,AskVolume5,ErrorID
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
}