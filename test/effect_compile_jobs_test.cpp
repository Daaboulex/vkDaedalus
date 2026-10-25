#include "overlay/effect_compile_jobs.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace vkBasalt;
using namespace std::chrono_literals;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

template <typename Take>
static std::optional<EffectAddBatch> within(Take take, std::chrono::milliseconds limit)
{
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until)
    {
        if (std::optional<EffectAddBatch> batch = take())
            return batch;
        std::this_thread::sleep_for(5ms);
    }
    return std::nullopt;
}

static std::vector<EffectCompileJobs::Compile> waitingOn(std::shared_future<void> gate, std::atomic<int>& runs, int count)
{
    std::vector<EffectCompileJobs::Compile> compiles;
    for (int i = 0; i < count; i++)
        compiles.push_back([gate, &runs] {
            runs++;
            gate.wait();
        });
    return compiles;
}

int main()
{
    {
        std::promise<void>       release;
        std::shared_future<void> released = release.get_future().share();
        std::atomic<int>         reads{0};
        std::atomic<int>         warms{0};
        EffectCompileJobs        jobs;

        jobs.start({{{"first", "cas_fx", "/fx/first.fx"}}, size_t{1}}, waitingOn(released, reads, 1));
        jobs.start({{{"second", "smaa_fx", "/fx/second.fx"}}, std::nullopt}, waitingOn(released, reads, 1));

        const auto asked = std::chrono::steady_clock::now();
        expect(!jobs.takeParametersRead() && !jobs.takeReady(), "a batch whose parameter read is still running is not handed on");
        expect(std::chrono::steady_clock::now() - asked < 100ms, "asking for finished compiles does not wait for one");
        expect(jobs.compiling() == std::vector<std::string>{"first", "second"}, "both waiting effects are listed as compiling");

        release.set_value();
        const std::optional<EffectAddBatch> read = within([&] { return jobs.takeParametersRead(); }, 5000ms);
        expect(read && read->effects[0].instanceName == "first", "the first batch comes back once its parameter read ends");
        expect(!jobs.takeReady(), "a batch whose chain compiles were never started does not join");
        expect(!jobs.takeParametersRead(), "a batch is handed back from its parameter read only once");

        std::promise<void>       chainRelease;
        std::shared_future<void> chainReleased = chainRelease.get_future().share();
        jobs.warmChain(waitingOn(chainReleased, warms, 2));
        expect(!jobs.takeReady(), "a batch joins only after the chain's own compiles finish");
        chainRelease.set_value();
        const std::optional<EffectAddBatch> first = within([&] { return jobs.takeReady(); }, 5000ms);
        expect(first && first->effects[0].instanceName == "first" && first->position == size_t{1},
               "the first batch joins with the position it was added at");
        expect(warms == 2, "every chain compile ran before the join");

        const std::optional<EffectAddBatch> secondRead = within([&] { return jobs.takeParametersRead(); }, 5000ms);
        jobs.warmChain({});
        const std::optional<EffectAddBatch> second = within([&] { return jobs.takeReady(); }, 5000ms);
        expect(secondRead && second && second->effects[0].instanceName == "second", "batches join in the order they were started");
        expect(jobs.compiling().empty(), "nothing is listed once every batch has joined");
        expect(reads == 2, "each batch read its parameters once");
    }

    {
        std::promise<void>       release;
        std::shared_future<void> released = release.get_future().share();
        std::atomic<int>         runs{0};
        EffectCompileJobs        jobs;
        jobs.start({{{"dropped", "dls_fx", "/fx/dropped.fx"}, {"dropped2", "dls_fx", "/fx/dropped.fx"}}, std::nullopt},
                   waitingOn(released, runs, 3));
        while (runs == 0)
            std::this_thread::sleep_for(1ms);
        jobs.cancel();
        expect(jobs.compiling().empty(), "a cancelled batch is no longer listed as compiling");

        jobs.start({{{"later", "cas", ""}}, std::nullopt}, {});
        const std::optional<EffectAddBatch> later = within([&] { return jobs.takeParametersRead(); }, 1000ms);
        expect(later && later->effects[0].instanceName == "later", "a cancelled batch still compiling does not hold back a later one");

        release.set_value();
        std::this_thread::sleep_for(100ms);
        expect(runs == 1, "a cancelled batch starts none of its remaining compiles");
        jobs.warmChain({});
        const std::optional<EffectAddBatch> joined = within([&] { return jobs.takeReady(); }, 1000ms);
        expect(joined && joined->effects[0].instanceName == "later", "only the later batch joins, never the cancelled one");
    }

    {
        std::promise<void>       release;
        std::shared_future<void> released = release.get_future().share();
        std::atomic<int>         runs{0};
        std::thread              releaser;
        {
            EffectCompileJobs jobs;
            jobs.start({{{"closing", "dls_fx", "/fx/closing.fx"}}, std::nullopt}, waitingOn(released, runs, 3));
            while (runs == 0)
                std::this_thread::sleep_for(1ms);
            releaser = std::thread([&release] {
                std::this_thread::sleep_for(50ms);
                release.set_value();
            });
        }
        releaser.join();
        expect(runs == 1, "tearing the jobs down waits only for the compile running now, not the rest of the batch");
    }

    {
        EffectCompileJobs jobs;
        jobs.start({{{"cas", "cas", ""}}, std::nullopt}, {});
        const std::optional<EffectAddBatch> builtIn = within([&] { return jobs.takeParametersRead(); }, 5000ms);
        expect(builtIn.has_value(), "a built-in effect, which has no file, comes back without a compile");
    }

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: added effects read their parameters, warm the chain's compiles off the frame, join in order, and stop on cancel\n");
    return 0;
}
