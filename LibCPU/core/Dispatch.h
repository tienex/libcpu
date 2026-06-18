/** @file
  A small libdispatch-like concurrent work queue.

  DispatchQueue::Async hands a job to a pool of worker threads, the way GCD's
  dispatch_async hands a block to a concurrent queue. LibCPU uses it to run the
  expensive things OFF the execution thread: optimizing (tier-1) recompiles and
  on-disk translation-cache I/O. The running machine never stalls on a slow
  compile -- the interpreter keeps it live until the background artifact is ready
  and the run loop hot-swaps it in at a safe point.

  Portable (std::thread / std::mutex / std::condition_variable) rather than tied
  to Apple's libdispatch, so the same mechanism serves every host.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_DISPATCH_H
#define LIBCPU_DISPATCH_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace LibCPU {

class DispatchQueue {
public:
    explicit DispatchQueue (unsigned Workers) : m_Stop (false) {
        if (Workers < 1) { Workers = 1; }
        for (unsigned I = 0; I < Workers; I++) {
            m_Threads.emplace_back ([this] { Worker (); });
        }
    }
    // Joins the workers; jobs still queued when the queue is destroyed are dropped (the owner tears
    // down the queue BEFORE the state its jobs touch, so an in-flight job always sees live state).
    ~DispatchQueue () {
        {
            std::unique_lock<std::mutex> Lk (m_Mutex);
            m_Stop = true;
        }
        m_Cv.notify_all ();
        for (std::thread &T : m_Threads) {
            if (T.joinable ()) { T.join (); }
        }
    }

    DispatchQueue (const DispatchQueue &) = delete;
    DispatchQueue &operator= (const DispatchQueue &) = delete;

    void Async (std::function<void ()> Job) {
        {
            std::unique_lock<std::mutex> Lk (m_Mutex);
            m_Jobs.push (std::move (Job));
        }
        m_Cv.notify_one ();
    }

private:
    void Worker () {
        for (;;) {
            std::function<void ()> Job;
            {
                std::unique_lock<std::mutex> Lk (m_Mutex);
                m_Cv.wait (Lk, [this] { return m_Stop || !m_Jobs.empty (); });
                if (m_Stop && m_Jobs.empty ()) { return; }
                Job = std::move (m_Jobs.front ());
                m_Jobs.pop ();
            }
            Job ();
        }
    }

    std::vector<std::thread>           m_Threads;
    std::queue<std::function<void ()>> m_Jobs;
    std::mutex                         m_Mutex;
    std::condition_variable            m_Cv;
    bool                               m_Stop;
};

} // namespace LibCPU

#endif // LIBCPU_DISPATCH_H
