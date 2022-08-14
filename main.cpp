#include "PlayerEngine.h"
#include "YMLConfig.hpp"
#include <stdio.h>
#include <stdlib.h>

void printHelp()
{
    printf("Usage:  XServer -f ~/config.yml -d\n");
    printf("\t-f: Config File Path\n");
    printf("\t-a: Account\n");
    printf("\t-d: log debug mode, print debug log\n");
    printf("\t-h: print help infomartion\n");
}

int main(int argc, char *argv[])
{
    std::string ConfigPath = "./config.yml";
    std::string Account;
    int ch;
    bool debug = false;
    while ((ch = getopt(argc, argv, "f:a:dh")) != -1)
    {
        switch (ch)
        {
        case 'f':
            ConfigPath = optarg;
            break;
        case 'a':
            Account = optarg;
            break;
        case 'd':
            debug = true;
            break;
        case 'h':
        case '?':
        case ':':
        default:
            printHelp();
            exit(-1);
            break;
        }
    }
    std::string path;
    char* p = getenv("APP_LOG_PATH");
    if(p == NULL)
    {
        path = "./log/";
    }
    else
    {
        path = p;
    }
    Utils::gLogger = Utils::Singleton<Utils::Logger>::GetInstance();
    Utils::gLogger->setLogPath(path, "XDataPlayer_" + Account);
    Utils::gLogger->Init();
    Utils::gLogger->setDebugLevel(debug);
    PlayerEngine engine;
    engine.LoadConfig(ConfigPath.c_str());
    engine.Start();
    return 0;
}