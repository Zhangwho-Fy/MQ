#ifndef __M_CHANNEL_H__
#define __M_CHANNEL_H__

#include "muduo/net/TcpConnection.h"
#include "muduo/proto/codec.h"
#include "muduo/proto/dispatcher.h"

#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_msg.pb.h"
#include "../mqcommon/mq_proto.pb.h"
#include "../mqcommon/mq_threadpool.hpp"

#include "mq_consumer.hpp"
#include "mq_host.hpp"
#include "mq_route.hpp"

namespace Fy_mq{
    using ProtobuffCodecPtr = std::shared_ptr<ProtobufCodec>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using closeChannelRequestPtr = std::shared_ptr<closeChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
    using openChannelRequestPtr = std::shared_ptr<openChannelRequest>;
};

#endif