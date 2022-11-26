#ifndef HPPACKCLIENT_H
#define HPPACKCLIENT_H

#include "HPSocket4C.h"
#include "Logger.h"
#include "PackMessage.hpp"
#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>
#include "LockFreeQueue.hpp"
#include "MarketData.hpp"

class HPPackClient
{
public:
    HPPackClient(const char* ip, unsigned int port);
    void Start();
    void Stop();
    static void SendData(HP_Client pClient, const unsigned char* pBuffer, int iLength);
    void SendData(const unsigned char* pBuffer, int iLength);
    virtual ~HPPackClient();
    static Utils::LockFreeQueue<Message::PackMessage> m_MarketDataMessageQueue;
protected:
    static En_HP_HandleResult __stdcall OnConnect(HP_Client pSender, HP_CONNID dwConnID);
    static En_HP_HandleResult __stdcall OnSend(HP_Server pSender, HP_CONNID dwConnID, const BYTE* pData, int iLength);
    static En_HP_HandleResult __stdcall OnReceive(HP_Server pSender, HP_CONNID dwConnID, const BYTE* pData, int iLength);
    static En_HP_HandleResult __stdcall OnClose(HP_Server pSender, HP_CONNID dwConnID, En_HP_SocketOperation enOperation, int iErrorCode);
private:
    static std::string m_ServerIP;
    static unsigned int m_ServerPort;
    HP_TcpPackClient m_pClient;
    HP_TcpPackClientListener m_pListener;
    static bool m_Connected;
};

#endif // HPPACKCLIENT_H