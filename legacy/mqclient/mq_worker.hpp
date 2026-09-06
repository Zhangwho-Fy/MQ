#ifndef __M_WORKER_H__
#define __M_WORKER_H__
#include "muduo/net/EventLoopThread.h"
#include "../../common/mq_logger.hpp"
#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_threadpool.hpp"

namespace Fy_mq {
    class AsyncWorker {
        public:
            using ptr = std::shared_ptr<AsyncWorker>;
            muduo::net::EventLoopThread loopthread;
            threadpool pool;
    };
}

#endif