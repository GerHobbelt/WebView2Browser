#pragma once

#include <ppl.h>
#include <agents.h>

using namespace Concurrency;

template <typename T>
class async_future
{
public:
    template <class func1>
    explicit async_future(func1&& fn1)
    {
        // Execute the work function in a task group and send the result
        // to the single_assignment object.
        _tasks.run([fn1, this]() {
            send(_value, fn1());
        });
    }
    template <class func1, class func2>
    explicit async_future(func1&& fn1, func2&& fn2) : after(std::bind(fn2))
    {
        _tasks.run([fn1, this]() {
            send(_value, fn1());
        });
    }

    ~async_future()
    {
        _tasks.wait(); // Wait for the task to finish.
        if (after)
            after();
    }

    // Retrieves the result of the work function.
    // This method blocks if the async_future object is still computing the value.
    auto get() { return receive(_value); }
private:
    task_group _tasks; // Executes the asynchronous work function.
    single_assignment<T> _value; // Stores the result of the asynchronous work function.
    std::function<void()> after; // What to do synchronously after the given task is done
};