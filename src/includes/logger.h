#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

// 定义日志级别枚举
typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARNING = 3,
    LOG_ERROR = 4,
    LOG_FATAL = 5
} LogLevel;

extern std::string pnx_logger_perfix;

// 获取日志级别名称
const char* levelName(LogLevel level);

// 根据日志级别输出相应级别的日志信息
void _mylog(LogLevel level, const char *file, int line, const char *format, ...);

// 日志输出函数封装，添加了文件名和行号参数
#define logTrace(format, ...) _mylog(LOG_TRACE, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define logDebug(format, ...) _mylog(LOG_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define logInfo(format, ...) _mylog(LOG_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define logWarning(format, ...) _mylog(LOG_WARNING, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define logError(format, ...) _mylog(LOG_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define logFatal(format, ...) _mylog(LOG_FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)
