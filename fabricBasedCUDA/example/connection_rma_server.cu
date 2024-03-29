//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/cuda/connection.cuh>
#include <networklayer/cuda/gpu_buf.cuh>
#include <cuda.h>
#include <unistd.h>

int LOG_LEVEL = TRACE;

int main(int argc, char **argv) {

    cuInit(0);

    CUcontext ctx;

    CUdevice d;
    cuDeviceGet(&d, 0);

    if (cuCtxCreate(&ctx, CU_CTX_MAP_HOST, d) != CUDA_SUCCESS) {
        exit(1);
    }

    if(cuCtxSetCurrent(ctx) != CUDA_SUCCESS){
        exit(3);
    }

    cse498::unique_buf buf;

    char *gpu_buf, *cpu_buf;
    cpu_buf = new char[64 * 1024];

    if(cuMemAlloc((CUdeviceptr*) &gpu_buf, 64 * 1024) != CUDA_SUCCESS){
        exit(2);
    }

    {
        DO_LOG(DEBUG) << (void *) gpu_buf;
    }

    cse498::gpu_buf remoteAccess(gpu_buf, cpu_buf, 64 * 1024);

    const char *addr = "127.0.0.1";

    if (argc > 1) {
        addr = argv[1];
    }

    auto *c1 = new cse498::Connection(addr, true, 8080, cse498::Verbs);

    while (!c1->connect());

    *((uint64_t *) remoteAccess.getCPU()) = ~0;
    remoteAccess.moveToGPU();
    uint64_t key = 1;
    c1->register_mr(remoteAccess, FI_SEND | FI_RECV, key, true);
    uint64_t key2 = 2;
    c1->register_mr(buf, FI_SEND | FI_RECV | FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, key2);

    std::cerr << "Send\n";

    *((uint64_t *) buf.get()) = key;

    c1->send(buf, sizeof(uint64_t));

    std::cerr << "Send\n";

    *((uint64_t *) buf.get()) = (uint64_t) remoteAccess.get();

    c1->send(buf, sizeof(uint64_t));

    c1->send(remoteAccess, sizeof(uint64_t));

    std::cerr << "Recv\n";

    c1->recv(buf, 1);

    cuCtxDestroy(ctx);

    return 0;
}
