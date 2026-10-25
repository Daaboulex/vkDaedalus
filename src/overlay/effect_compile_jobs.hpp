#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "background_work.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    struct EffectAddition
    {
        std::string instanceName;
        std::string effectType;
        std::string path;
    };

    struct EffectAddBatch
    {
        std::vector<EffectAddition> effects;
        std::optional<size_t>       position;
    };

    class EffectCompileJobs
    {
    public:
        using Compile = std::function<void()>;

        EffectCompileJobs()                                    = default;
        EffectCompileJobs(const EffectCompileJobs&)            = delete;
        EffectCompileJobs& operator=(const EffectCompileJobs&) = delete;

        ~EffectCompileJobs()
        {
            cancel();
            for (std::future<void>& running : abandoned)
                running.wait();
        }

        void start(EffectAddBatch batch, std::vector<Compile> parameterReads)
        {
            Job job{std::move(batch), Stage::ReadingParameters, std::make_shared<std::atomic<bool>>(false), {}};
            job.done = run(std::move(parameterReads), job.cancelled);
            jobs.push_back(std::move(job));
        }

        std::optional<EffectAddBatch> takeParametersRead()
        {
            forgetFinishedAbandoned();
            if (jobs.empty() || jobs.front().stage != Stage::ReadingParameters || !finished(jobs.front().done))
                return std::nullopt;
            jobs.front().stage = Stage::ParametersRead;
            return jobs.front().batch;
        }

        void warmChain(std::vector<Compile> chainCompiles)
        {
            if (jobs.empty() || jobs.front().stage != Stage::ParametersRead)
            {
                Logger::err("a chain warm-up was started with no batch past its parameter read, so it is skipped");
                return;
            }
            jobs.front().done  = run(std::move(chainCompiles), jobs.front().cancelled);
            jobs.front().stage = Stage::WarmingChain;
        }

        std::optional<EffectAddBatch> takeReady()
        {
            if (jobs.empty() || jobs.front().stage != Stage::WarmingChain || !finished(jobs.front().done))
                return std::nullopt;
            EffectAddBatch batch = std::move(jobs.front().batch);
            jobs.pop_front();
            return batch;
        }

        std::vector<std::string> compiling() const
        {
            std::vector<std::string> names;
            for (const Job& job : jobs)
                for (const EffectAddition& effect : job.batch.effects)
                    names.push_back(effect.instanceName);
            return names;
        }

        void cancel()
        {
            for (Job& job : jobs)
            {
                job.cancelled->store(true);
                if (job.done.valid())
                    abandoned.push_back(std::move(job.done));
            }
            jobs.clear();
        }

    private:
        enum class Stage
        {
            ReadingParameters,
            ParametersRead,
            WarmingChain
        };

        struct Job
        {
            EffectAddBatch                     batch;
            Stage                              stage;
            std::shared_ptr<std::atomic<bool>> cancelled;
            std::future<void>                  done;
        };

        static std::future<void> run(std::vector<Compile> compiles, std::shared_ptr<std::atomic<bool>> cancelled)
        {
            try
            {
                BackgroundWork& work = BackgroundWork::instance();
                return std::async(std::launch::async, [compiles = std::move(compiles), cancelled, &work] {
                    const BackgroundWork::Running running(work);
                    for (const Compile& compile : compiles)
                    {
                        if (cancelled->load() || running.stopped())
                            return;
                        try
                        {
                            compile();
                        }
                        catch (const std::exception& e)
                        {
                            Logger::warn(std::string("a background compile for an added effect threw, so the chain compiles it itself: ")
                                         + e.what());
                        }
                    }
                });
            }
            catch (const std::system_error& e)
            {
                Logger::err(std::string("no thread could start for an added effect's compiles, so the chain compiles it itself: ")
                            + e.what());
                std::promise<void> skipped;
                skipped.set_value();
                return skipped.get_future();
            }
        }

        static bool finished(std::future<void>& done)
        {
            if (!done.valid())
                return true;
            if (done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return false;
            try
            {
                done.get();
            }
            catch (...)
            {
                Logger::warn("a background compile for an added effect ended with an unknown exception, so the chain compiles it itself");
            }
            return true;
        }

        void forgetFinishedAbandoned()
        {
            std::erase_if(abandoned, [](std::future<void>& running) { return finished(running); });
        }

        std::deque<Job>                jobs;
        std::vector<std::future<void>> abandoned;
    };
}
