// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_PARALLEL_H_
#define MESH_PROCESSING_LIBHH_PARALLEL_H_

#include "Hh.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// This macro can be set here, in Makefile, or at the top of any source file (if careful with precompiled headers).
// #define HH_NO_OPENMP

#if 0
{
    parallel_for_each(range(n), [&](const int i) { func(i); });
    parallel_for_int(i, n) { func(i); }
    cond_parallel_for_int(n*1000, i, n) { func_1000_instruction_cycles(i); } // only parallelize if beneficial
    int sum = 0; omp_parallel_for_T(reduction(+:sum) if(ar.num()>k_omp_thresh), int, i, 0, ar.num()) sum += ar[i];

}
#endif

#if defined(_MSC_VER) && defined(HH_DEBUG) && !defined(HH_NO_OPENMP)
// Bug in Visual Studio 2015 update 1; compilation creates buggy code in x64-Debug with OpenMP in:
//  MatrixOp.h: invert(CMatrixView<T> mi, MatrixView<T> mo)  (from Frame::invert() from tGeom.cpp);
//  Multigrid.h: dual_downsample_aux(Specialize<2>, CGridView<D,T> grid)  (from tMultigrid.cpp);
#define HH_NO_OPENMP
#endif

#if !defined(_OPENMP) && !defined(HH_NO_OPENMP) // if compiler feature is absent,  we disable the macros as well
#define HH_NO_OPENMP
#endif

#if !defined(HH_NO_OPENMP)
#include <omp.h>                // OpenMP
#endif

// Notes on using OpenMP:
// - Any OpenMP pragma must immediately precede the "for" statement,
//    so I must embed the OpenMP pragma inside the omp_parallel_for_T() macro.
// - With OpenMP, the "for" statement initializer may only declare a single variable,
//    so I must evaluate the upper-bound ub earlier in the omp_parallel_for_T() macro.
// - OpenMP version 2.0 (VS2010, 2013, 2015) only allows signed integral index types
//    (version 3.0 allows unsigned types),
//    so for now I must override the macros for parallel_for_size_t() on VS.


namespace hh {

inline int get_max_threads() { return max(int(std::thread::hardware_concurrency()), 1); }

#if defined(HH_NO_OPENMP)
 
#define HH_PRAGMA_OMP(...)
#define omp_parallel_for_T(omp_args, T, i, lb, ub) for_T(T, i, lb, ub)

#else  // defined(HH_NO_OPENMP)

#define HH_PRAGMA_OMP(...) HH_PRAGMA(omp __VA_ARGS__)

#if defined(HH_DEBUG) && 1      // allows more warnings but does not pre-evaluate the upper-bound ub

#define omp_parallel_for_T(omp_args, T, i, lb, ub)                            \
    HH_PRAGMA_OMP(parallel for omp_args) for (T i = lb; i<(ub); i++)

#else

#if defined(__clang__)
// for "add explicit braces to avoid dangling else"
#define HH_OMP_PUSH_WARNINGS HH_PRAGMA(clang diagnostic push) HH_PRAGMA(clang diagnostic ignored "-Wdangling-else")
#define HH_OMP_POP_WARNINGS HH_PRAGMA(clang diagnostic pop)
#elif defined(__GNUC__)
#define HH_OMP_PUSH_WARNINGS HH_PRAGMA(GCC diagnostic ignored "-Wparentheses") // "suggest explicit braces"
#define HH_OMP_POP_WARNINGS // With gcc, the pop would have to occur later outside the macro.
#pragma GCC diagnostic ignored "-Wparentheses" // strange; this alone does not work in a precompiled header
#else
#define HH_OMP_PUSH_WARNINGS
#define HH_OMP_POP_WARNINGS
#pragma warning(disable:4701)   // "potentially uninitialized local variable 'xx' used"
#endif
#define omp_parallel_for_T(omp_args, T, i, lb, ub)                            \
    omp_parallel_for_T_aux(omp_args, T, i, lb, ub, HH_UNIQUE_ID(u))
#define omp_parallel_for_T_aux(omp_args, T, i, lb, ub, u)                                   \
    HH_OMP_PUSH_WARNINGS if (hh::false_capture<T> u = ub) { HH_UNREACHABLE; } else          \
        HH_OMP_POP_WARNINGS HH_PRAGMA_OMP(parallel for omp_args) for (T i = lb; i<u(); i++)

#endif  // defined(HH_DEBUG)

#endif  // defined(HH_NO_OPENMP)


#define parallel_for_T(T, i, lb, ub)         omp_parallel_for_T(, T, i, lb, ub)
#define cond_parallel_for_T(c, T, i, lb, ub) omp_parallel_for_T(if((c)>=hh::k_omp_thresh), T, i, lb, ub)

#define parallel_for_int(i, ub)      parallel_for_T(int, i, 0, ub)
#define parallel_for_intL(i, lb, ub) parallel_for_T(int, i, lb, ub)
#define parallel_for_size_t(i, ub)   parallel_for_T(size_t, i, 0, ub)

// (Note: name parallel_for() conflicts with that in VC <ppl.h> )
#define hh_parallel_for(...) HH_PRAGMA_OMP(parallel for) for (__VA_ARGS__)

#define cond_parallel_for_int(c, i, ub)    cond_parallel_for_T(c, int, i, 0, ub)
#define cond_parallel_for_size_t(c, i, ub) cond_parallel_for_T(c, size_t, i, 0, ub)

const uint64_t k_omp_thresh = 100*1000; // number of instruction cycles above which a loop should be parallelized
const uint64_t k_omp_many_cycles_per_elem = 400; // number of instruction cycles for larger-overhead parallelism
const int k_omp_min_iterations = 8;              // sufficient number of loop iterations for parallelism

#if _OPENMP<200805 // (2.0==200203; 3.0==200805; 4.0==201307; VS2015 is still 2.0)
// http://dautovri.blogspot.com/2015/04/check-your-openmp-version.html
// Loop variable cannot be unsigned, so use a signed one instead.
#undef parallel_for_size_t
#define parallel_for_size_t(i, ub)  parallel_for_T(intptr_t, i, 0, static_cast<intptr_t>(ub))
#undef cond_parallel_for_size_t
#define cond_parallel_for_size_t(c, i, ub) cond_parallel_for_T(c, intptr_t, i, 0, static_cast<intptr_t>(ub))
#endif


// Launches a set of threads whose number matches the hardware parallelism, and terminates these threads upon
// destruction.  The member function execute(num_tasks, task_function) allows parallel execution of an indexed task,
// i.e. calling task_function(0), ..., task_function(num_tasks-1) and waiting for all these calls to finish.
// The function execute() can be called successively on different tasks with low overhead as it reuses the
// same set of threads.
class ThreadPoolIndexedTask : noncopyable {
 public:
    using Task = std::function<void(int)>;
    ThreadPoolIndexedTask() {
        const int num_threads = get_max_threads();
        _threads.reserve(num_threads);
        for_int(i, num_threads) _threads.emplace_back(&ThreadPoolIndexedTask::worker_main, this);
    }
    ~ThreadPoolIndexedTask() {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            assertx(!_num_remaining_tasks);
            _running = false;
            _condition_variable_worker.notify_all();
        }
        for (auto& thread : _threads) thread.join();
    }
    int num_threads() const                 { return int(_threads.size()); }
    bool already_active() const             { return _num_remaining_tasks != 0; }  // detect nested execution
    void execute(int num_tasks, const Task& task_function) {
        if (already_active()) {
            Warning("Nested execution of ThreadPoolIndexedTask is run serially");
            for_int(i, num_tasks) task_function(i);
        } else {
            std::unique_lock<std::mutex> lock(_mutex);
            _task_function = task_function;
            _num_tasks = num_tasks;
            _num_remaining_tasks = num_tasks;
            _task_index = 0;
            _condition_variable_worker.notify_all();
            while (_num_remaining_tasks) _condition_variable_master.wait(lock);
        }
    }
    static ThreadPoolIndexedTask& default_threadpool() {
        static unique_ptr<ThreadPoolIndexedTask> thread_pool;
        // This is safe because thread_pool is nullptr only in the main thread before any other thread is launched.
        if (!thread_pool) thread_pool = make_unique<ThreadPoolIndexedTask>();
        return *thread_pool;
    }
    
 private:
    std::mutex _mutex;
    bool _running = true;
    std::vector<std::thread> _threads;
    Task _task_function;
    int _num_tasks;
    int _num_remaining_tasks = 0;
    int _task_index;
    std::condition_variable _condition_variable_worker;
    std::condition_variable _condition_variable_master;

    void worker_main() {
        std::unique_lock<std::mutex> lock(_mutex);
        // Consider: https://stackoverflow.com/questions/233127/how-can-i-propagate-exceptions-between-threads
        // However, rethrowing the exception in the main thread loses the stack state, so not useful for debugging.
        while (_running) {
            // _condition_variable_worker.wait(lock, [this]() { return _task_index<_num_tasks || !_running; });
            _condition_variable_worker.wait(lock);
            for (;;) {
                if (_task_index==_num_tasks) break;
                int i = _task_index++;
                lock.unlock();
                _task_function(i);
                lock.lock();
                if (!--_num_remaining_tasks) _condition_variable_master.notify_all();
            }
        }
    }
};

constexpr uint64_t k_parallelism_always = k_omp_thresh;

// Evaluates function(element) for each element in range by parallelizing across chunks of elements using
// a cached thread pool.  The range must support begin/end functions returning random-access iterators.
// Parallelism is disabled if the estimated cost (estimated_cycles_per_element * size(range)) is less than some
// internal threshold, or if this parallel_for_each() is already executing (nested) within another parallel_for_each().
// Exceptions within function() cause program termination as they are not caught.
// One drawback over OpenMP is that if an exception or abort occurs within function(), the stack trace will not
// include the functions that called parallel_for_each() because these lie in the stack frames of a different thread.
template <typename Range, typename Function = void(size_t)>
void parallel_for_each(const Range& range, const Function& function,
                       uint64_t estimated_cycles_per_element = k_parallelism_always) {
    // using std::size; const size_t num_elements = size(range);  // C++17
    using std::begin;
    using std::end;
    const auto begin_range = begin(range);
    const auto end_range = end(range);
    const size_t num_elements = size_t(end_range - begin_range);
    using iterator_category = typename std::iterator_traits<decltype(begin_range)>::iterator_category;
    static_assert(std::is_same<iterator_category, std::random_access_iterator_tag >::value,
                  "Range iterator is not random-access");
    uint64_t total_num_cycles = num_elements * estimated_cycles_per_element;
    constexpr bool use_omp = false;  // OpenMP seems to never be faster, so there is no reason to use it here.
    if (use_omp) {
        cond_parallel_for_size_t(total_num_cycles, index, num_elements) {
            function(begin_range[index]);
        }
    } else {
        const int max_num_threads = get_max_threads();
        const int num_threads = int(min<size_t>(max_num_threads, num_elements));
        const bool desire_parallelism = num_threads > 1 && total_num_cycles >= k_omp_thresh;
        ThreadPoolIndexedTask* const thread_pool =
            desire_parallelism ? &ThreadPoolIndexedTask::default_threadpool() : nullptr;
        if (!thread_pool || thread_pool->already_active()) {
            // Traverse the range elements sequentially.
            for (size_t index = 0; index < num_elements; ++index) {
                function(begin_range[index]);
            }
        } else {
            // Traverse the range elements in parallel.
            const size_t chunk_size = (num_elements + num_threads - 1) / num_threads;
            thread_pool->execute(num_threads, [begin_range, num_elements, chunk_size, &function](int thread_index) {
                size_t index_start = thread_index * chunk_size;
                size_t index_stop = std::min((thread_index + 1) * chunk_size, num_elements);
                for (size_t index = index_start; index < index_stop; ++index) {
                    function(begin_range[index]);
                }
            });
        }
    }
}

} // namespace hh

#endif // MESH_PROCESSING_LIBHH_PARALLEL_H_
