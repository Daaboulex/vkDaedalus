#pragma once

#include <mutex>

namespace vkBasalt
{
    inline thread_local bool layerLockHeld = false;

    class LayerLock
    {
    public:
        void lock()
        {
            mutex.lock();
            layerLockHeld = true;
        }

        bool try_lock()
        {
            if (!mutex.try_lock())
                return false;
            layerLockHeld = true;
            return true;
        }

        void unlock()
        {
            layerLockHeld = false;
            mutex.unlock();
        }

    private:
        std::mutex mutex;
    };
}
