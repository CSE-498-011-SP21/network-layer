//
// Created by depaulsmiller on 3/10/21.
//

#include <networklayer/fabricBased.hh>

void registerReturnPackAs1(cse498::FabricRPC &f) {
    f.registerRPC(1, [](cse498::pack_t p) {
        return p;
    });
}