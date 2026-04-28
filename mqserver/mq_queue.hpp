#ifndef __M_QUEUE_H__
#define __M_QUEUE_H__

#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_msg.pb.h"

#include <iostream>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Fy_mq{
    struct MsgQueue{
        using ptr = std::shared_ptr<MsgQueue>;

        std::string name;
        bool durable;
        bool exclusive;
        bool auto_delete;
        google::protobuf::Map<std::string,std::string> args;

        MsgQueue() = default;
        MsgQueue(const std::string& qname,
                bool qdurable,
                bool qexclusive,
                bool qauto_delete,
                const google::protobuf::Map<std::string, std::string>& qargs)
            : name(qname),
            durable(qdurable),
            exclusive(qexclusive),
            auto_delete(qauto_delete),
            args(qargs) {}
        
        void SetArgs(const std::string &str_args){
            std::vector<std::string> sub_args;
            StrHelper::split(str_args,"&",sub_args);
            for(auto &str : sub_args){
                size_t pos = str.find("=");
                if( pos == std::string::npos){
                    ELOG("交换机解析格式非法:%s",str.c_str());
                    continue;//跳过非法格式
                }
                std::string key = str.substr(0,pos);
                std::string value = str.substr(pos+1);
                args[key] = value;
            }
        }

        std::string getArgs() const {
            std::string result;
            for(auto &it:args){
                result += it.first + "=" + it.second + "&";
            }
            return result;
        }
    };
}

#endif