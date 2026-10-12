#include <iostream>
#include "EventLoop.h"
#include "TcpServer.h"

int main()
{
    TcpServer tcpserver;
    tcpserver.Start(TcpServer::ReactorModel::PEER_TO_PEER);
    getchar();
    tcpserver.Close();
#ifdef DEBUG
    std::cout << "调试Version" << std::endl;
#else
    std::cout << "发布Version" << std::endl;
#endif
    return 0;
}