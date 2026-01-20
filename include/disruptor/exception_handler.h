#pragma once

#include <exception>
#include <sstream>
#include <string>

#include "NanoLogCpp17.h"
#include "backward.hpp"

namespace disruptor
{
using NanoLog::LogLevels::ERROR;
using NanoLog::LogLevels::WARNING;

namespace detail
{
inline std::string buildStackTrace()
{
    backward::StackTrace stackTrace;
    stackTrace.load_here(64);
    backward::Printer printer;
    printer.object = true;
    printer.color_mode = backward::ColorMode::automatic;
    std::ostringstream out;
    printer.print(stackTrace, out);
    return out.str();
}

inline std::string exceptionMessage(std::exception_ptr exception)
{
    if (!exception)
    {
        return "unknown exception";
    }

    try
    {
        std::rethrow_exception(exception);
    }
    catch (const std::exception& ex)
    {
        return ex.what();
    }
    catch (...)
    {
        return "non-std exception";
    }
}
}

template <typename T>
class ExceptionHandler
{
public:
    virtual ~ExceptionHandler() = default;
    virtual void handleEventException(std::exception_ptr exception, long sequence, T* event) = 0;
    virtual void handleOnStartException(std::exception_ptr exception) = 0;
    virtual void handleOnShutdownException(std::exception_ptr exception) = 0;
};

template <typename T>
class FatalExceptionHandler final : public ExceptionHandler<T>
{
public:
    void handleEventException(std::exception_ptr exception, long sequence, T* event) override
    {
        logException("Exception processing", exception, sequence, event);
        throw std::runtime_error(detail::exceptionMessage(exception));
    }

    void handleOnStartException(std::exception_ptr exception) override
    {
        logException("Exception during onStart()", exception, -1, nullptr);
    }

    void handleOnShutdownException(std::exception_ptr exception) override
    {
        logException("Exception during onShutdown()", exception, -1, nullptr);
    }

private:
    void logException(const char* context, std::exception_ptr exception, long sequence, T* event)
    {
        const std::string message = detail::exceptionMessage(exception);
        const std::string stack = detail::buildStackTrace();
        NANO_LOG(ERROR,
            "%s: %s\nSequence: %ld\nEvent: %p\nStack:\n%s",
            context, message.c_str(), sequence, static_cast<void*>(event), stack.c_str());
    }
};

template <typename T>
class IgnoreExceptionHandler final : public ExceptionHandler<T>
{
public:
    void handleEventException(std::exception_ptr exception, long sequence, T* event) override
    {
        logException("Exception processing", exception, sequence, event);
    }

    void handleOnStartException(std::exception_ptr exception) override
    {
        logException("Exception during onStart()", exception, -1, nullptr);
    }

    void handleOnShutdownException(std::exception_ptr exception) override
    {
        logException("Exception during onShutdown()", exception, -1, nullptr);
    }

private:
    void logException(const char* context, std::exception_ptr exception, long sequence, T* event)
    {
        const std::string message = detail::exceptionMessage(exception);
        const std::string stack = detail::buildStackTrace();
        NANO_LOG(WARNING,
            "%s: %s\nSequence: %ld\nEvent: %p\nStack:\n%s",
            context, message.c_str(), sequence, static_cast<void*>(event), stack.c_str());
    }
};

template <typename T>
class ExceptionHandlers
{
public:
    static ExceptionHandler<T>& defaultHandler()
    {
        static FatalExceptionHandler<T> handler;
        return handler;
    }
};
} // namespace disruptor
