// =============================================================================
// Basic/Logger.h
// 通用日志类（跨整个 TitusGLRenderer 解决方案使用）。
//   - 单例 + 线程安全（内部 std::mutex）
//   - 同时输出到控制台与本地日志文件
//   - 日志文件路径固定为 {SolutionDir}/Logs/，文件名带启动时间戳
//   - 提供 printf 风格 (LOG_INFO 等) 与流式风格 (LOG_STREAM_INFO 等) 双 API
//   - 零外部依赖，仅依赖 C++17 标准库
// =============================================================================
#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>

namespace TitusBasic
{
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info  = 2,
        Warn  = 3,
        Error = 4,
        Fatal = 5,
        Off   = 6,
    };

    class Logger
    {
    public:
        static Logger& Instance();

        // 可选显式初始化；不调用也会在首次 Log 时自动 lazy 初始化。
        // appName 仅用于日志文件名后缀（缺省由可执行模块名推导）。
        void Init(const std::string& appName = std::string());

        void SetMinLevel(LogLevel level) { m_minLevel.store(static_cast<int>(level), std::memory_order_relaxed); }
        LogLevel GetMinLevel() const     { return static_cast<LogLevel>(m_minLevel.load(std::memory_order_relaxed)); }

        void SetConsoleEnabled(bool enabled) { m_consoleEnabled.store(enabled, std::memory_order_relaxed); }
        void SetFileEnabled(bool enabled)    { m_fileEnabled.store(enabled, std::memory_order_relaxed); }

        const std::string& GetLogFilePath() const { return m_logFilePath; }
        // Init 时传入（或由 exe 名推导）的应用名；未 Init 时为空。
        const std::string& GetAppName() const { return m_appName; }

        // printf 风格
        void Logf(LogLevel level, const char* tag, const char* fmt, ...);
        void LogfV(LogLevel level, const char* tag, const char* fmt, va_list args);

        // 已格式化字符串
        void LogLine(LogLevel level, const char* tag, const std::string& message);

        // 强制刷盘
        void Flush();

        // 程序退出前可显式 Shutdown，否则由析构函数自动处理。
        void Shutdown();

        ~Logger();

    private:
        Logger();
        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;

        void EnsureInitialized();
        void InitImpl(const std::string& appName);
        std::string ResolveLogDir() const;
        static const char* LevelName(LogLevel level);
        static const char* LevelColorEscape(LogLevel level);  // 控制台 ANSI 颜色（可选）

        std::atomic<int>  m_minLevel{ static_cast<int>(LogLevel::Trace) };
        std::atomic<bool> m_consoleEnabled{ true };
        std::atomic<bool> m_fileEnabled{ true };

        std::mutex   m_mutex;
        bool         m_initialized = false;
        std::ofstream m_file;
        std::string  m_logFilePath;
        std::string  m_appName;
    };

    // 流式日志辅助对象：在析构时把累积的内容一次性提交给 Logger
    class LogStreamHelper
    {
    public:
        LogStreamHelper(LogLevel level, const char* tag)
            : m_level(level), m_tag(tag ? tag : "") {}

        ~LogStreamHelper()
        {
            Logger::Instance().LogLine(m_level, m_tag, m_stream.str());
        }

        std::ostringstream& Stream() { return m_stream; }

        template <typename T>
        LogStreamHelper& operator<<(const T& v)
        {
            m_stream << v;
            return *this;
        }

        // 兼容 std::endl / std::hex 这类流操控符
        LogStreamHelper& operator<<(std::ostream& (*manip)(std::ostream&))
        {
            m_stream << manip;
            return *this;
        }
        LogStreamHelper& operator<<(std::ios_base& (*manip)(std::ios_base&))
        {
            m_stream << manip;
            return *this;
        }

    private:
        LogLevel    m_level;
        const char* m_tag;
        std::ostringstream m_stream;
    };
} // namespace TitusBasic

// ===== 宏：printf 风格 =====================================================
#define LOG_TRACE(tag, ...) ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Trace, tag, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Debug, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Info,  tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Warn,  tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Error, tag, __VA_ARGS__)
#define LOG_FATAL(tag, ...) ::TitusBasic::Logger::Instance().Logf(::TitusBasic::LogLevel::Fatal, tag, __VA_ARGS__)

// ===== 宏：流式（兼容旧 std::cerr/std::cout 用法） =========================
#define LOG_STREAM_TRACE(tag) ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Trace, tag)
#define LOG_STREAM_DEBUG(tag) ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Debug, tag)
#define LOG_STREAM_INFO(tag)  ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Info,  tag)
#define LOG_STREAM_WARN(tag)  ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Warn,  tag)
#define LOG_STREAM_ERROR(tag) ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Error, tag)
#define LOG_STREAM_FATAL(tag) ::TitusBasic::LogStreamHelper(::TitusBasic::LogLevel::Fatal, tag)
