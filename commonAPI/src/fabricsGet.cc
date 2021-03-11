#include <networklayer/config.hh>
#include <rdma/fabric.h>
#include <iostream>

int main(int argc, char **argv) {

    std::cout << "Using Network version: " << NETWORK_VER << std::endl;

    std::cout << "Using Fabric version : " << FI_MAJOR_VERSION << ":" << FI_MINOR_VERSION << std::endl;

    std::cout << std::endl;

    const int version = FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION);

    fi_info* info;

    const char* node = nullptr;

    if(fi_getinfo(version, node, nullptr, 0, nullptr, &info) != 0){
        return 1;
    }

    fi_info* start = info;

    while(start){
        std::cout << start->fabric_attr->name << std::endl;
        std::cout << (start->caps & FI_ATOMIC ? "FI_ATOMIC" : "NO FI_ATOMIC") << std::endl;
        std::cout << (start->caps & FI_FENCE ? "FI_FENCE" : "NO FI_FENCE") << std::endl;
        std::cout << (start->caps & FI_MSG ? "FI_MSG" : "NO FI_MSG") << std::endl;
        std::cout << (start->caps & FI_READ ? "FI_READ" : "NO FI_READ") << std::endl;
        std::cout << (start->caps & FI_RMA ? "FI_RMA" : "NO FI_RMA") << std::endl;
        std::cout << (start->caps & FI_HMEM ? "HAS FI_HMEM" : "NO FI_HMEM") << std::endl;

        std::cout << std::endl;
        start = start->next;
    }

    fi_freeinfo(info);

    return 0;
}