#ifndef __M_CONSUMER_H__
#define __M_CONSUMER_H__

#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_msg.pb.h"


namespace Fy_mq{
    using ConsumerCallback = std::function<void(const std::string &, const BasicProperties *bp, const std::string &)>;
    struct Consumer {
        using ptr = std::shared_ptr<Consumer>;

        std::string tag;
        std::string qname;
        bool auto_ack;
        ConsumerCallback callback;
        
        Consumer(){
            DLOG("new Consumer: %p",this);
        }

        Consumer(const std::string &ctag,const std::string &queue_name,bool ack_flag,const ConsumerCallback &cb):
            tag(ctag),qname(queue_name),auto_ack(ack_flag),callback(std::move(cb)){
            DLOG("new Consumer: %p",this);
        }

        ~Consumer(){
            DLOG("del Consumer: %p", this);
        }
    };
};


#endif