//
// Created by depaulsmiller on 2/18/21.
//

#include <fabricBased.hh>
#include <thread>

int main(){
    cse498::FabricRPC f;
    f.registerRPC(1, [](cse498::pack_t p){
        return p;
    });

    std::cerr << "Here\n";

    f.start();

    return 0;
}