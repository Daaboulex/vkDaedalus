#include "gpu_chain.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace vkBasalt;

static int failures = 0;

template <typename Handle>
static Handle fake(uintptr_t value)
{
    return (Handle) value;
}

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

struct Submit
{
    VkQueue     queue;
    VkSemaphore wait;
    VkSemaphore signal;
    VkFence     fence;
};

static const VkDevice gameDevice = fake<VkDevice>(0x300);
static const VkDevice deviceB    = fake<VkDevice>(0x400);
static const VkDevice deviceC    = fake<VkDevice>(0x500);
static const VkQueue  gameQueue  = fake<VkQueue>(0x100);
static const VkQueue  queueB     = fake<VkQueue>(0x200);
static const VkQueue  queueC     = fake<VkQueue>(0x210);

static std::vector<Submit> submits;
static std::vector<int>    exported;
static int                 failSubmitAt = -1;
static VkDevice            failExportOn = VK_NULL_HANDLE;
static VkDevice            failImportOn = VK_NULL_HANDLE;
static bool                fenceRecreatedSignalled = false;

static VKAPI_ATTR VkResult VKAPI_CALL stubSubmit(VkQueue queue, uint32_t, const VkSubmitInfo* info, VkFence fence)
{
    if (static_cast<int>(submits.size()) == failSubmitAt)
        return VK_ERROR_DEVICE_LOST;
    submits.push_back({queue, info->waitSemaphoreCount ? info->pWaitSemaphores[0] : VK_NULL_HANDLE,
                       info->signalSemaphoreCount ? info->pSignalSemaphores[0] : VK_NULL_HANDLE, fence});
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL stubExport(VkDevice device, const VkSemaphoreGetFdInfoKHR*, int* fd)
{
    if (device == failExportOn)
        return VK_ERROR_TOO_MANY_OBJECTS;
    int ends[2];
    if (pipe(ends) != 0)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    close(ends[1]);
    *fd = ends[0];
    exported.push_back(ends[0]);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL stubImport(VkDevice device, const VkImportSemaphoreFdInfoKHR* info)
{
    if (device == failImportOn)
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    close(info->fd);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL stubReset(VkDevice, uint32_t, const VkFence*)
{
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL stubDestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}

static VKAPI_ATTR VkResult VKAPI_CALL stubCreateFence(VkDevice, const VkFenceCreateInfo* info, const VkAllocationCallbacks*, VkFence* fence)
{
    fenceRecreatedSignalled = (info->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    *fence                  = fake<VkFence>(0x999);
    return VK_SUCCESS;
}

static bool closed(int fd)
{
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

static DeviceDispatch stubs()
{
    DeviceDispatch vkd{};
    vkd.QueueSubmit          = stubSubmit;
    vkd.GetSemaphoreFdKHR    = stubExport;
    vkd.ImportSemaphoreFdKHR = stubImport;
    vkd.ResetFences          = stubReset;
    vkd.DestroyFence         = stubDestroyFence;
    vkd.CreateFence          = stubCreateFence;
    return vkd;
}

static ChainSegment segment(uint32_t gpu, uintptr_t base, bool fenced)
{
    ChainSegment s;
    s.gpu            = gpu;
    s.start          = {fake<VkSemaphore>(base + 1)};
    s.done           = {fake<VkSemaphore>(base + 2)};
    s.commandBuffers = {fake<VkCommandBuffer>(base + 3)};
    if (fenced)
        s.fences = {fake<VkFence>(base + 4)};
    return s;
}

struct Run
{
    HandOff     handOff;
    GpuChain    chain;
    VkSemaphore present = fake<VkSemaphore>(0x50);
    VkFence     effect  = fake<VkFence>(0x60);
};

static Run handOffWith(const std::vector<uint32_t>& gpus)
{
    submits.clear();
    exported.clear();
    fenceRecreatedSignalled = false;
    static DeviceDispatch vkd;
    vkd = stubs();

    Run                      run;
    std::vector<HandOffSide> sides;
    run.chain.headDone = {fake<VkSemaphore>(0x10)};
    for (size_t k = 0; k < gpus.size(); k++)
    {
        run.chain.segments.push_back(segment(gpus[k], 0x1000 * (k + 1), gpus[k] != 1));
        sides.push_back(gpus[k] == 2 ? HandOffSide{&vkd, deviceB, queueB} : gpus[k] == 3 ? HandOffSide{&vkd, deviceC, queueC} : HandOffSide{&vkd, gameDevice, gameQueue});
    }
    VkSubmitInfo head{};
    head.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore appWait = fake<VkSemaphore>(0x40);
    head.waitSemaphoreCount   = 1;
    head.pWaitSemaphores      = &appWait;
    run.handOff = submitThroughChain({&vkd, gameDevice, gameQueue}, sides, run.chain, 0, head, run.present, run.effect);
    return run;
}

static void resetFailures()
{
    failSubmitAt = -1;
    failExportOn = VK_NULL_HANDLE;
    failImportOn = VK_NULL_HANDLE;
}

int main()
{
    {
        resetFailures();
        const Run run = handOffWith({2, 1});
        expect(run.handOff.handedOver && run.handOff.failure.empty(), "a chain to GPU 2 and back is handed over");
        expect(submits.size() == 3, "three submits: the head, GPU 2, the hand-back");
        expect(submits.size() == 3 && submits[0].queue == gameQueue && submits[0].wait == fake<VkSemaphore>(0x40)
                   && submits[0].signal == run.chain.headDone[0] && submits[0].fence == VK_NULL_HANDLE,
               "the head waits the game's semaphores and signals the hand-off, with no fence");
        expect(submits.size() == 3 && submits[1].queue == queueB && submits[1].wait == run.chain.segments[0].start[0]
                   && submits[1].signal == run.chain.segments[0].done[0] && submits[1].fence == run.chain.segments[0].fences[0],
               "GPU 2 waits its imported start, signals its done and its own fence");
        expect(submits.size() == 3 && submits[2].queue == gameQueue && submits[2].wait == run.chain.segments[1].start[0]
                   && submits[2].signal == run.present && submits[2].fence == run.effect,
               "the hand-back waits GPU 2, signals what the present waits and the frame's fence");
        for (int fd : exported)
            expect(closed(fd), "every exported fd is owned once imported");
    }
    {
        resetFailures();
        const Run run = handOffWith({2, 1, 3, 1});
        expect(run.handOff.handedOver, "a chain across GPU 2, back to GPU 1, GPU 3 and back is handed over");
        expect(submits.size() == 5, "five submits for four segments and the head");
        expect(submits.size() == 5 && submits[1].queue == queueB && submits[2].queue == gameQueue && submits[3].queue == queueC
                   && submits[4].queue == gameQueue,
               "the segments go to their GPUs' queues in order");
        expect(submits.size() == 5 && submits[2].fence == VK_NULL_HANDLE && submits[2].signal == run.chain.segments[1].done[0],
               "a segment on GPU 1 mid-chain signals its done with no fence of its own");
        expect(submits.size() == 5 && submits[3].wait == run.chain.segments[2].start[0] && submits[4].signal == run.present
                   && submits[4].fence == run.effect,
               "GPU 3 waits its start, and only the last segment signals the present and the frame's fence");
        expect(exported.size() == 4, "one hand-off fd per crossing");
    }
    {
        resetFailures();
        failSubmitAt  = 0;
        const Run run = handOffWith({2, 1});
        expect(!run.handOff.handedOver && !run.handOff.headRan && exported.empty(), "a failed head submit exports nothing");
    }
    {
        resetFailures();
        failExportOn  = gameDevice;
        const Run run = handOffWith({2, 1});
        expect(!run.handOff.handedOver && run.handOff.headRan && run.handOff.fallbackWait == run.chain.headDone[0],
               "a failed export of the head's hand-off makes the fallback wait it");
    }
    for (VkDevice importer : {deviceB, gameDevice})
    {
        resetFailures();
        failImportOn  = importer;
        const Run run = handOffWith({2, 1});
        expect(!run.handOff.handedOver && run.handOff.fallbackWait == VK_NULL_HANDLE && !run.handOff.failure.empty(),
               std::string("a failed import on ") + (importer == deviceB ? "GPU 2" : "the game's GPU") + " ends the hand-off with a reason");
        expect(!exported.empty() && closed(exported.back()), "the fd a failed import did not take is closed");
    }
    {
        resetFailures();
        failSubmitAt  = 1;
        const Run run = handOffWith({2, 1});
        expect(!run.handOff.handedOver && fenceRecreatedSignalled, "a failed submit on GPU 2 puts its reset fence back signalled");
        expect(run.handOff.failure.find("GPU 2") != std::string::npos, "the reason names GPU 2");
    }
    {
        resetFailures();
        failSubmitAt  = 2;
        const Run run = handOffWith({2, 1});
        expect(!run.handOff.handedOver && run.handOff.headRan && !fenceRecreatedSignalled, "a failed hand-back submit is reported, the frame left to the fallback");
    }

    if (failures)
        std::printf("%d hand-off check(s) failed\n", failures);
    return failures ? 1 : 0;
}
