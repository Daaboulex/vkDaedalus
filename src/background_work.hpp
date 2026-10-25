#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <mutex>

namespace vkBasalt
{
    class BackgroundWork
    {
    public:
        static BackgroundWork& instance()
        {
            static BackgroundWork* const work = [] {
                BackgroundWork* created = new BackgroundWork();
                std::atexit([] { BackgroundWork::instance().stopAll(); });
                return created;
            }();
            return *work;
        }

        class Running
        {
        public:
            explicit Running(BackgroundWork& work) : work(work), started(work.begin()) {}
            Running(const Running&)            = delete;
            Running& operator=(const Running&) = delete;
            ~Running()
            {
                if (started)
                    work.end();
            }
            bool stopped() const { return !started || work.stopRequested(); }

        private:
            BackgroundWork& work;
            const bool      started;
        };

    private:
        bool begin()
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping)
                return false;
            running++;
            return true;
        }

        void end()
        {
            std::lock_guard<std::mutex> lock(mutex);
            running--;
            idle.notify_all();
        }

        bool stopRequested()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return stopping;
        }

        void stopAll()
        {
            std::unique_lock<std::mutex> lock(mutex);
            stopping = true;
            idle.wait(lock, [this] { return running == 0; });
        }

        std::mutex              mutex;
        std::condition_variable idle;
        size_t                  running  = 0;
        bool                    stopping = false;
    };
}
