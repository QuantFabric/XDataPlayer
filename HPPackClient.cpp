#include "HPPackClient.h"

bool HPPackClient::m_Connected = false;
extern Utils::Logger* gLogger;

std::string HPPackClient::m_ServerIP;
unsigned int HPPackClient::m_ServerPort;

Utils::LockFreeQueue<Message::PackMessage> HPPackClient::m_MarketDataMessageQueue(1 << 10);

HPPackClient::HPPackClient(const char* ip, unsigned int port)
{
    m_ServerIP = ip;
    m_ServerPort = port;
    // 创建监听器对象
    m_pListener = ::Create_HP_TcpPackClientListener();
    // 创建 Socket 对象
    m_pClient = ::Create_HP_TcpPackClient(m_pListener);
    // 设置 Socket 监听器回调函数
    ::HP_Set_FN_Client_OnConnect(m_pListener, OnConnect);
    ::HP_Set_FN_Client_OnSend(m_pListener, OnSend);
    ::HP_Set_FN_Client_OnReceive(m_pListener, OnReceive);
    ::HP_Set_FN_Client_OnClose(m_pListener, OnClose);

    HP_TcpPackClient_SetMaxPackSize(m_pClient, 0xFFFF);
    HP_TcpPackClient_SetPackHeaderFlag(m_pClient, 0x169);
}

void HPPackClient::Start()
{
    if (m_Connected)
        return;
    char errorString[512] = { 0 };
    if (::HP_Client_Start(m_pClient, (LPCTSTR)m_ServerIP.c_str(), m_ServerPort, false))
    {
        sprintf(errorString, "HPPackClient::Start connected to server[%s:%d]", m_ServerIP.c_str(), m_ServerPort);
        Utils::gLogger->Log->info(errorString);
        sleep(1);
        Message::PackMessage message;
        message.MessageType = Message::EMessageType::ELoginRequest;
        message.LoginRequest.ClientType = Message::EClientType::EXDATAPLAYER;
        strncpy(message.LoginRequest.Account, "XDataPlayer", sizeof(message.LoginRequest.Account));
        SendData((const unsigned char*)&message, sizeof (message));
    }
    else
    {
        sprintf(errorString, "HPPackClient::Start connected to server[%s:%d] failed, error code:%d error massage:%s",
                m_ServerIP.c_str(), m_ServerPort, ::HP_Client_GetLastError(m_pClient), HP_Client_GetLastErrorDesc(m_pClient));
        Utils::gLogger->Log->warn(errorString);
    }
}

void HPPackClient::Stop()
{
    ::HP_Client_Stop(m_pClient);
    //记录客户端(::HP_Client_GetConnectionID(m_pClient));
}

void HPPackClient::SendData(HP_Client pClient, const unsigned char* pBuffer, int iLength)
{
    static std::vector<Message::PackMessage> bufferQueue;
    if(!m_Connected)
    {
        Utils::gLogger->Log->warn("HPPackClient::SendData failed, disconnected to server");
        Message::PackMessage message;
        memset(&message, 0, sizeof(message));
        memcpy(&message, pBuffer, sizeof(message));
        bufferQueue.push_back(message);
        return;
    }
    for (size_t i = 0; i < bufferQueue.size(); i++)
    {
        ::HP_Client_Send(pClient, reinterpret_cast<const unsigned char *>(&bufferQueue.at(i)), sizeof(bufferQueue.at(i)));
    }
    bufferQueue.clear();
    bool ret = ::HP_Client_Send(pClient, pBuffer, iLength);
    if (!ret)
    {
        char errorString[128] = { 0 };
        sprintf(errorString, "HPPackClient::SendData failed, sys error code:%d, error code:%d, error message:%s",
                SYS_GetLastError(), HP_Client_GetLastError(pClient), HP_Client_GetLastErrorDesc(pClient));
        Utils::gLogger->Log->warn(errorString);
    }
}
void HPPackClient::SendData(const unsigned char* pBuffer, int iLength)
{
    static std::vector<Message::PackMessage> bufferQueue;
    if(!m_Connected)
    {
        Utils::gLogger->Log->warn("HPPackClient::SendData failed, disconnected to server");
        Message::PackMessage message;
        memset(&message, 0, sizeof(message));
        memcpy(&message, pBuffer, sizeof(message));
        bufferQueue.push_back(message);
        return;
    }
    for (size_t i = 0; i < bufferQueue.size(); i++)
    {
        ::HP_Client_Send(m_pClient, reinterpret_cast<const unsigned char *>(&bufferQueue.at(i)), sizeof(bufferQueue.at(i)));
    }
    bufferQueue.clear();
    bool ret = ::HP_Client_Send(m_pClient, pBuffer, iLength);
    if (!ret)
    {
        char errorString[128] = { 0 };
        sprintf(errorString,
                "HPPackClient::SendData failed, sys error code:%d, error code:%d, error message:%s",
                SYS_GetLastError(), HP_Client_GetLastError(m_pClient), HP_Client_GetLastErrorDesc(m_pClient));
        Utils::gLogger->Log->warn(errorString);
    }
}

HPPackClient::~HPPackClient()
{
    // 销毁 Socket 对象
    ::Destroy_HP_TcpPackClient(m_pClient);
    // 销毁监听器对象
    ::Destroy_HP_TcpPackClientListener(m_pListener);
}

En_HP_HandleResult __stdcall HPPackClient::OnConnect(HP_Client pSender, HP_CONNID dwConnID)
{
    TCHAR szAddress[50];
    int iAddressLen = sizeof(szAddress) / sizeof(TCHAR);
    USHORT usPort;
    ::HP_Client_GetLocalAddress(pSender, szAddress, &iAddressLen, &usPort);
    m_Connected = true;
    char errorString[128] = { 0 };
    sprintf(errorString,
            "HPPackClient::OnConnect %s:%d connected to server[%s:%d], dwConnID:%d",
            szAddress, usPort, m_ServerIP.c_str(), m_ServerPort, dwConnID);
    Utils::gLogger->Log->info(errorString);

    return HR_OK;
}

En_HP_HandleResult __stdcall HPPackClient::OnSend(HP_Server pSender, HP_CONNID dwConnID, const BYTE* pData, int iLength)
{
    return HR_OK;
}

En_HP_HandleResult __stdcall HPPackClient::OnReceive(HP_Server pSender, HP_CONNID dwConnID, const BYTE* pData, int iLength)
{
    unsigned int MessageType = *((unsigned int*)pData);
    Message::PackMessage message;
    memcpy(&message, pData, iLength);
    while(!m_MarketDataMessageQueue.Push(message));
    return HR_OK;
}

En_HP_HandleResult __stdcall HPPackClient::OnClose(HP_Server pSender, HP_CONNID dwConnID, En_HP_SocketOperation enOperation, int iErrorCode)
{
    TCHAR szAddress[50];
    int iAddressLen = sizeof(szAddress) / sizeof(TCHAR);
    USHORT usPort;
    ::HP_Client_GetLocalAddress(pSender, szAddress, &iAddressLen, &usPort);
    char errorString[128] = { 0 };
    sprintf(errorString,
            "HPPackClient::OnClose %s:%d disconnected to server[%s:%d], connID:%d",
            szAddress, usPort, m_ServerIP.c_str(), m_ServerPort, dwConnID);
    Utils::gLogger->Log->warn(errorString);
    m_Connected = false;
    return HR_OK;
}