#include <iostream>
#include <ctime>
#include <cstdio>

// 日志级别
#define DBG_LEVEL 0
#define INF_LEVEL 1
#define ERR_LEVEL 2
#define DEFAULT_LEVEL DBG_LEVEL

// 颜色宏定义
#define COLOR_NONE  "\033[0m"
#define COLOR_DBG   "\033[33m"  // 黄色
#define COLOR_INF   "\033[32m"  // 绿色
#define COLOR_ERR   "\033[31m"  // 红色

#define LOG(lev_color, lev_str, level, format,...) \
do{ \
    if(level >= DEFAULT_LEVEL){ \
        time_t t = time(nullptr); \
        struct tm *ptm = localtime(&t); \
        char time_str[32]; \
        strftime(time_str,31,"%H:%M:%S",ptm); \
        printf("[%s%s%s][%s][%s:%d]\t" format "\n",\
        lev_color, lev_str, COLOR_NONE ,time_str, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
}while(0)

#define DLOG(format,...) LOG(COLOR_DBG, "DBG", DBG_LEVEL, format,##__VA_ARGS__)
#define ILOG(format,...) LOG(COLOR_INF, "INF", INF_LEVEL, format,##__VA_ARGS__)
#define ELOG(format,...) LOG(COLOR_ERR, "ERR", ERR_LEVEL, format,##__VA_ARGS__)

int main(){
    DLOG("调试日志 hello");
    ILOG("普通日志 num = %d", 666);
    ELOG("错误日志 出错啦");
    return 0;
}
