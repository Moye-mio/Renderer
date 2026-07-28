// =============================================================================
// Basic/Logger.cpp
// =============================================================================
#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace TitusBasic
{
    // ---------- 静态/工具函数 ------------------------------------------------
    Logger& Logger::Instance()
    {
        static Logger s_inst;
        return s_inst;
    }

    Logger::Logger() = default;

    Logger::~Logger()
    {
        Shutdown();
    }

    const char* Logger::LevelName(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "?????";
        }
    }

    const char* Logger::LevelColorEscape(LogLevel level)
    {
        // 当前不强制启用颜色，留作扩展
        switch (level)
        {
            case LogLevel::Warn:  return "\033[33m";  // yellow
            case LogLevel::Error: return "\033[31m";  // red
            case LogLevel::Fatal: return "\033[1;31m";// bold red
            default:              return "";
        }
    }

    // ---------- SolutionDir / Logs 目录解析 ---------------------------------
    // 优先级：
    //   1) 编译期宏 SOLUTION_DIR（各 vcxproj 已经定义为 R"($(SolutionDir))"）
    //   2) 可执行文件路径向上回溯，寻找含 *.sln 或同名 Logs 目录的祖先
    //   3) 当前工作目录下的 Logs/
    std::string Logger::ResolveLogDir() const
    {
        namespace fs = std::filesystem;

#ifdef SOLUTION_DIR
        try
        {
            fs::path solDir = fs::path(SOLUTION_DIR);
            if (!solDir.empty() && fs::exists(solDir))
            {
                fs::path logs = solDir / "Logs";
                std::error_code ec;
                fs::create_directories(logs, ec);
                return logs.string();
            }
        }
        catch (...) { /* fallthrough */ }
#endif

        // exe 目录回溯
        fs::path exeDir;
#if defined(_WIN32)
        char buf[MAX_PATH] = {};
        DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            exeDir = fs::path(buf).parent_path();
        }
#endif
        if (exeDir.empty())
        {
            std::error_code ec;
            exeDir = fs::current_path(ec);
        }

        for (fs::path p = exeDir; !p.empty(); p = p.parent_path())
        {
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(p, ec))
            {
                if (!ec && entry.is_regular_file() &&
                    entry.path().extension() == ".sln")
                {
                    fs::path logs = p / "Logs";
                    fs::create_directories(logs, ec);
                    return logs.string();
                }
            }
            if (p == p.root_path()) break;
        }

        // fallback：当前工作目录下 Logs/
        fs::path fallback = fs::current_path() / "Logs";
        std::error_code ec;
        fs::create_directories(fallback, ec);
        return fallback.string();
    }

    // ---------- 初始化 ------------------------------------------------------
    void Logger::Init(const std::string& appName)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_initialized) return;
        InitImpl(appName);
    }

    void Logger::EnsureInitialized()
    {
        if (m_initialized) return;
        InitImpl(std::string());
    }

    void Logger::InitImpl(const std::string& appName)
    {
        namespace fs = std::filesystem;

        // 解析名称
        std::string name = appName;
        if (name.empty())
        {
#if defined(_WIN32)
            char buf[MAX_PATH] = {};
            DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH)
            {
                name = fs::path(buf).stem().string();
            }
#endif
            if (name.empty()) name = "TitusApp";
        }

        // 时间戳
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char ts[32] = {};
        std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tmv);

        std::string logDir = ResolveLogDir();
        fs::path filePath = fs::path(logDir) / (std::string(ts) + "_" + name + ".log");
        m_logFilePath = filePath.string();

        m_file.open(m_logFilePath, std::ios::out | std::ios::trunc);
        if (m_file.is_open())
        {
            m_file << "==== TitusBasic::Logger started: " << ts
                   << " app=" << name << " ====\n";
            m_file.flush();
        }
        else
        {
            // 文件打开失败，禁用文件输出但仍允许控制台
            m_fileEnabled.store(false, std::memory_order_relaxed);
            std::fprintf(stderr,
                "[Logger] failed to open log file: %s\n", m_logFilePath.c_str());
        }

        m_initialized = true;
    }

    void Logger::Shutdown()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_file.is_open())
        {
            m_file << "==== TitusBasic::Logger shutdown ====\n";
            m_file.flush();
            m_file.close();
        }
        m_initialized = false;
    }

    // ---------- 输出实现 ----------------------------------------------------
    void Logger::Logf(LogLevel level, const char* tag, const char* fmt, ...)
    {
        if (static_cast<int>(level) < m_minLevel.load(std::memory_order_relaxed))
            return;

        va_list ap;
        va_start(ap, fmt);
        LogfV(level, tag, fmt, ap);
        va_end(ap);
    }

    void Logger::LogfV(LogLevel level, const char* tag, const char* fmt, va_list args)
    {
        if (static_cast<int>(level) < m_minLevel.load(std::memory_order_relaxed))
            return;

        // 第一遍试着写入栈缓冲；若不够则按返回值再扩
        char  stackBuf[1024];
        va_list copy;
        va_copy(copy, args);
        int n = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt ? fmt : "", copy);
        va_end(copy);

        std::string out;
        if (n < 0)
        {
            out = "<format error>";
        }
        else if (static_cast<size_t>(n) < sizeof(stackBuf))
        {
            out.assign(stackBuf, static_cast<size_t>(n));
        }
        else
        {
            std::vector<char> dyn(static_cast<size_t>(n) + 1);
            std::vsnprintf(dyn.data(), dyn.size(), fmt ? fmt : "", args);
            out.assign(dyn.data(), static_cast<size_t>(n));
        }

        LogLine(level, tag, out);
    }

    void Logger::LogLine(LogLevel level, const char* tag, const std::string& message)
    {
        if (static_cast<int>(level) < m_minLevel.load(std::memory_order_relaxed))
            return;

        std::lock_guard<std::mutex> lk(m_mutex);
        EnsureInitialized();

        // 时间戳
        auto now    = std::chrono::system_clock::now();
        auto tt     = std::chrono::system_clock::to_time_t(now);
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()).count() % 1000;
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        char timeBuf[32] = {};
        std::snprintf(timeBuf, sizeof(timeBuf),
            "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
            static_cast<long long>(millis));

        // 去掉消息末尾多余换行（兼容旧代码里写了 "\n" 的情况）
        std::string body = message;
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
            body.pop_back();

        // 组装一行
        // 形如: [2026-06-11 00:07:23.123][INFO ][TitusRHI] message
        std::string line;
        line.reserve(body.size() + 64);
        line += '[';
        line += timeBuf;
        line += "][";
        line += LevelName(level);
        line += "][";
        line += (tag && *tag) ? tag : "-";
        line += "] ";
        line += body;
        line += '\n';

        // 写文件
        if (m_fileEnabled.load(std::memory_order_relaxed) && m_file.is_open())
        {
            m_file.write(line.data(), static_cast<std::streamsize>(line.size()));
            // Error 及以上立刻 flush，便于崩溃前不丢日志
            if (level >= LogLevel::Error) m_file.flush();
        }

        // 控制台
        if (m_consoleEnabled.load(std::memory_order_relaxed))
        {
            FILE* dst = (level >= LogLevel::Warn) ? stderr : stdout;
            std::fwrite(line.data(), 1, line.size(), dst);
            if (level >= LogLevel::Error) std::fflush(dst);
        }
    }

    void Logger::Flush()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_file.is_open()) m_file.flush();
        std::fflush(stdout);
        std::fflush(stderr);
    }
} // namespace TitusBasic
