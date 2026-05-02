#ifndef __M_ROUTE_H__
#define __M_ROUTE_H__

#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_msg.pb.h"

namespace Fy_mq{
    class Router{
    public:
        static bool isLegalRoutingKey(const std::string &routing_key){
            //routing_key：只需要判断是否包含有非法字符即可， 合法字符( a~z, A~Z, 0~9, ., _)
            for (auto &ch : routing_key) {
                if ((ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    (ch == '_' || ch == '.')) {
                    continue;
                }
                return false;
            }
            return true;
        }

        static bool isLegalBindingKey(const std::string &binding_key){
            //1. 判断是否包含有非法字符， 合法字符：a~z, A~Z, 0~9, ., _, *, #
            for (auto &ch : binding_key) {
                if ((ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    (ch == '_' || ch == '.' || ch == '*' || ch == '#'))  {
                    continue;
                }
                return false;
            }
            //2. *和#必须独立存在:  news.music#.*.#
            std::vector<std::string> sub_words;
            StrHelper::split(binding_key,".",sub_words);
            for(auto &word : sub_words){
                if(word.size() > 1 && (word.find("*") != std::string::npos || word.find("#") != std::string::npos))
                    return false;
            }
            //3. *和#不能连续出现，#和#不能连续出现，
            int n = sub_words.size();
            for(int i = 1; i < n; ++i){
                if(sub_words[i] == "#" && (sub_words[i - 1] == "*" || sub_words[i - 1] == "#")){
                    return false;
                }else if(sub_words[i] == "*" && sub_words[i - 1] == "#"){
                    return false;
                }
            }
            return true;
        }

        static bool route(ExchangeType type,const std::string &routing_key,const std::string &binding_key){
            if(type == ExchangeType::DIRECT){
                return routing_key == binding_key;
            }else if(type == ExchangeType::FANOUT){
                return true;
            }else{
                std::vector<std::string> rkeys,bkeys;
                int n_rkeys = StrHelper::split(routing_key,".",rkeys);
                int n_bkeys = StrHelper::split(binding_key,".",bkeys);
                std::vector<bool> dp(n_bkeys + 1,false);
                dp[0] = true;
                if(n_bkeys && bkeys[0] == "#") dp[1] = true;
                for(int i = 0;i < n_rkeys; ++i){
                    if(i)dp[0] = false;
                    bool pre = dp[0],tmp;
                    for(int j = 0;j < n_bkeys; ++j){
                        tmp = dp[j + 1];
                        if(rkeys[i] == bkeys[j] || bkeys[j] == "*"){
                            dp[j + 1] = pre;
                        }else if(bkeys[j] == "#"){
                            //1个都不匹配或者匹配一个
                            dp[j + 1] = dp[j] | dp[j + 1];
                        }else{
                            dp[j + 1] = false;
                        }
                        pre = tmp;
                    }
                }
                return dp[n_bkeys];
            }
        }
    };

}
#endif