#include "logger.h"

std::string pnx_logger_perfix = "NA";

// 获取日志级别名称
const char* levelName(LogLevel level) {
    switch (level) {
        case LOG_TRACE:
            return "TRACE";
        case LOG_DEBUG:
            return "DEBUG";
        case LOG_INFO:
            return "INFO";
        case LOG_WARNING:
            return "WARNING";
        case LOG_ERROR:
            return "ERROR";
        case LOG_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

// 设置默认的日志级别为LOG_DEBUG
static LogLevel logLevel = LOG_DEBUG;
static int enableColor = 1;

// 设置环境变量LOG_LEVEL来动态修改日志级别
static void setLogLevelFromEnv() {
    char *env_level = getenv("PNX_LOG_LEVEL");
    if (env_level != NULL) {
        if (strcmp(env_level, "TRACE") == 0) {
            logLevel = LOG_TRACE;
        } else if (strcmp(env_level, "DEBUG") == 0) {
            logLevel = LOG_DEBUG;
        } else if (strcmp(env_level, "INFO") == 0) {
            logLevel = LOG_INFO;
        } else if (strcmp(env_level, "WARNING") == 0) {
            logLevel = LOG_WARNING;
        } else if (strcmp(env_level, "ERROR") == 0) {
            logLevel = LOG_ERROR;
        } else if (strcmp(env_level, "FATAL") == 0) {
            logLevel = LOG_FATAL;
        } else {
            printf("Wrong log level name.\n");
            exit(-1);
        }
    }
}

static void setLogColorFromEnv() {
    char *env_level = getenv("PNX_NOCOLOR");
    if (env_level == NULL) {
        enableColor = 1;
    } else enableColor = 0;
}

// 获取当前时间，精确到毫秒
static char* getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t nowtime = tv.tv_sec;
    struct tm *nowtm = localtime(&nowtime);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", nowtm);
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), ".%03ld", tv.tv_usec / 1000);
    return strdup(buffer);
}

// 根据日志级别输出相应级别的日志信息
void _mylog(LogLevel level, const char *file, int line, const char *format, ...) {
    static int init = 0;
    if (!init) {
        setLogLevelFromEnv();
        setLogColorFromEnv();
        init = 1;
    }

    if (level >= logLevel) {
        const char *type_color = "";
        switch (level) {
            case LOG_TRACE:
                type_color = "\033[36m"; // Cyan color
                break;
            case LOG_DEBUG:
                type_color = "\033[32m"; // Green color
                break;
            case LOG_INFO:
                type_color = "\033[34m"; // Blue color
                break;
            case LOG_WARNING:
                type_color = "\033[33m"; // Yellow color
                break;
            case LOG_ERROR:
                type_color = "\033[31m"; // Red color
                break;
            case LOG_FATAL:
                type_color = "\033[35m"; // Magenta color
                break;
            default:
                break;
        }

        const char *filename = strrchr(file, '/');
        if (filename != NULL) {
            filename++;  // 跳过斜杠字符
        } else {
            filename = file;
        }

        va_list args;
        va_start(args, format);
        char message[1024];
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        if (enableColor) {
            printf("[%s %s] %s[%s] %s:%d\033[0m %s\n",
                pnx_logger_perfix.c_str(), getCurrentTime(), type_color, 
                levelName(level), filename, line, message);
        } else {
            printf("[%s %s] [%s] %s:%d %s\n",
                pnx_logger_perfix.c_str(), getCurrentTime(), 
                levelName(level), filename, line, message);
        }
    }
}
