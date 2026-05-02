#ifndef __M_HOST_H__
#define __M_HOST_H__

#include "mq_exchange.hpp"
#include "mq_queue.hpp"
#include "mq_binding.hpp"
#include "mq_message.hpp"

namespace Fy_mq {
    // 虚拟主机类：MQ 核心调度层
    // 聚合 交换机、队列、绑定、消息 四大模块，提供统一访问入口
    class VirtualHost {
    public:
        using ptr = std::shared_ptr<VirtualHost>;

        // 构造：初始化四大管理器，并为所有队列恢复消息
        VirtualHost(const std::string &hname, 
                    const std::string &basedir, 
                    const std::string &dbfile)
            : _host_name(hname)
            , _emp(std::make_shared<ExchangeManager>(dbfile))
            , _mqmp(std::make_shared<MsgQueueManager>(dbfile))
            , _bmp(std::make_shared<BindingManager>(dbfile))
            , _mmp(std::make_shared<MessageManager>(basedir)) 
        {
            // 启动恢复：初始化所有队列的消息管理器
            QueueMap qm = _mqmp->allQueues();
            for (auto &q : qm) {
                _mmp->initQueueMessage(q.first);
            }
        }

        // 声明交换机
        bool declareExchange(const std::string &name,
            ExchangeType type, bool durable, bool auto_delete,
            const google::protobuf::Map<std::string, std::string> &args) 
        {
            return _emp->declareExchange(name, type, durable, auto_delete, args);
        }

        // 删除交换机（同时删除相关绑定）
        void deleteExchange(const std::string &name) {
            _bmp->removeExchangeBindings(name);
            _emp->deleteExchange(name);
        }

        bool existsExchange(const std::string &name) {
            return _emp->exists(name);
        }

        Exchange::ptr selectExchange(const std::string &name) {
            return _emp->selectExchange(name);
        }

        // 声明队列（同时初始化消息存储）
        bool declareQueue(const std::string &qname,
            bool qdurable,
            bool qexclusive,
            bool qauto_delete,
            const google::protobuf::Map<std::string, std::string> &qargs) 
        {
            _mmp->initQueueMessage(qname);
            return _mqmp->declareQueue(qname, qdurable, qexclusive, qauto_delete, qargs);
        }

        // 删除队列（同时删除消息 + 绑定）
        void deleteQueue(const std::string &name) {
            _mmp->destroyQueueMessage(name);
            _bmp->removeMsgqueueBindings(name);
            _mqmp->deleteQueue(name);
        }

        bool existsQueue(const std::string &name) {
            return _mqmp->exists(name);
        }

        QueueMap allQueues() {
            return _mqmp->allQueues();
        }

        // 绑定：交换机 + 队列 + 路由键
        bool bind(const std::string &ename, const std::string &qname, const std::string &key) {
            Exchange::ptr ep = _emp->selectExchange(ename);
            if (!ep) {
                DLOG("绑定失败：交换机 %s 不存在", ename.c_str());
                return false;
            }
            MsgQueue::ptr mqp = _mqmp->selectQueue(qname);
            if (!mqp) {
                DLOG("绑定失败：队列 %s 不存在", qname.c_str());
                return false;
            }
            // 持久化绑定 = 交换机持久化 && 队列持久化
            return _bmp->bind(ename, qname, key, ep->durable && mqp->durable);
        }

        // 解绑
        void unBind(const std::string &ename, const std::string &qname) {
            _bmp->unBind(ename, qname);
        }

        MsgQueueBindMap ExchangeBindings(const std::string &ename) {
            return _bmp->getExchangeBindings(ename);
        }

        bool existsBinding(const std::string &ename, const std::string &qname) {
            return _bmp->exists(ename, qname);
        }

        // 发布消息
        bool basicPublish(const std::string &qname, BasicProperties *bp, const std::string &body) {
            MsgQueue::ptr mqp = _mqmp->selectQueue(qname);
            if (!mqp) {
                DLOG("消息发布失败：队列 %s 不存在", qname.c_str());
                return false;
            }
            // 传入队列持久化属性，决定消息是否持久化
            return _mmp->insert(qname, bp, body, mqp->durable);
        }

        // 消费消息
        MessagePtr basicConsume(const std::string &qname) {
            return _mmp->front(qname);
        }

        // 确认消息
        void basicAck(const std::string &qname, const std::string &msg_id) {
            _mmp->ack(qname, msg_id);
        }

        // 清空 vhost 所有数据
        void clear() {
            _emp->clear();
            _mqmp->clear();
            _bmp->clear();
            _mmp->clear();
        }

    private:
        std::string _host_name;
        ExchangeManager::ptr _emp;       // 交换机管理器
        MsgQueueManager::ptr _mqmp;      // 队列管理器
        BindingManager::ptr _bmp;        // 绑定管理器
        MessageManager::ptr _mmp;        // 消息持久化管理器
    };
}

#endif