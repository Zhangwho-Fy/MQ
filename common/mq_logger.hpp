#ifndef MQ_COMMON_LOGGER_HPP
#define MQ_COMMON_LOGGER_HPP

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace mq {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Off = 6,
};

inline const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF";
    }
    return "UNKNOWN";
}

inline LogLevel parseLogLevel(const char* name) {
    if (name == nullptr) return LogLevel::Debug;
    if (std::strcmp(name, "trace") == 0) return LogLevel::Trace;
    if (std::strcmp(name, "debug") == 0) return LogLevel::Debug;
    if (std::strcmp(name, "info") == 0)  return LogLevel::Info;
    if (std::strcmp(name, "warn") == 0 || std::strcmp(name, "warning") == 0) {
        return LogLevel::Warn;
    }
    if (std::strcmp(name, "error") == 0) return LogLevel::Error;
    if (std::strcmp(name, "fatal") == 0) return LogLevel::Fatal;
    if (std::strcmp(name, "off") == 0)   return LogLevel::Off;
    return LogLevel::Debug;
}

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return min_level_;
    }

    // Redirect all log records to an append-only file. Falls back to stderr.
    void setFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_ != stderr && file_ != stdout) {
            std::fclose(file_);
        }
        file_ = std::fopen(path.c_str(), "a");
        if (file_ == nullptr) file_ = stderr;
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::fflush(file_);
    }

    void log(LogLevel level, const char* file, int line, const char* format, ...) {
        va_list args;
        va_start(args, format);
        vlog(level, file, line, format, args);
        va_end(args);
    }

private:
    Logger()
        : file_(stderr),
          min_level_(parseLogLevel(std::getenv("MQ_LOG_LEVEL"))) {
        const char* path = std::getenv("MQ_LOG_FILE");
        if (path != nullptr && *path != '\0') {
            FILE* output = std::fopen(path, "a");
            if (output != nullptr) file_ = output;
        }
    }

    ~Logger() {
        if (file_ != stderr && file_ != stdout) std::fclose(file_);
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static std::string formatTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch())
                                .count() %
                            1000;
        std::tm local_time{};
#if defined(_WIN32)
        localtime_s(&local_time, &seconds);
#else
        localtime_r(&seconds, &local_time);
#endif
        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
        std::ostringstream output;
        output << buffer << '.' << static_cast<int>(millis);
        return output.str();
    }

    static std::string formatThreadId() {
        std::ostringstream output;
        output << std::this_thread::get_id();
        return output.str();
    }

    void vlog(LogLevel level, const char* file, int line, const char* format,
              va_list args) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (min_level_ == LogLevel::Off || level < min_level_) return;

        const std::string timestamp = formatTimestamp();
        const std::string thread_id = formatThreadId();
        std::fprintf(file_, "[%s][%s][tid=%s][%s:%d] ", timestamp.c_str(),
                     logLevelName(level), thread_id.c_str(), file, line);
        std::vfprintf(file_, format, args);
        std::fputc('\n', file_);
        std::fflush(file_);
    }

    mutable std::mutex mutex_;
    FILE* file_;
    LogLevel min_level_;
};

}  // namespace mq

#define TLOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define DLOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define ILOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define WLOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define ELOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define FLOG(...) \
    ::mq::Logger::instance().log(::mq::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)

#endif  // MQ_COMMON_LOGGER_HPP
