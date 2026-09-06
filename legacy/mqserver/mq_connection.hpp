#include "mq_channel.hpp"

namespace Fy_mq{
    class Connection{
    public:
        using ptr = std::shared_ptr<Connection>;

        Connection(const VirtualHost::ptr &host,
            const ConsumerManager::ptr &cmp,
            const ProtobufCodecPtr &codec,
            const muduo::net::TcpConnectionPtr &conn,
            const threadpool::ptr &pool):
            _conn(conn),
            _codec(codec),
            _cmp(cmp),
            _host(host),
            _pool(pool),
            _channels(std::make_shared<ChannelManager>()){}
        
        void openChannel(const openChannelRequestPtr &req){
            bool ret = _channels->openChannel(req->cid(),_host,_cmp,_codec,_conn,_pool);
            if(ret == false){
                DLOG("创建信道的时候，信道ID重复了");
                basicResponse(false, req->rid(), req->cid());
                return;
            }
            DLOG("%s 信道创建成功！", req->cid().c_str());
            basicResponse(true, req->rid(), req->cid());
        }

        void closeChannel(const closeChannelRequestPtr &req){
            _channels->closeChannel(req->cid());
            basicResponse(true, req->rid(), req->cid());
        }

        Channel::ptr getChannel(const std::string &cid){
            return _channels->getChannel(cid);
        }

    private:
        void basicResponse(bool ok,const std::string &rid,const std::string &cid){
            basicCommonResponse resp;
            resp.set_ok(ok);
            resp.set_rid(rid);
            resp.set_cid(cid);
            _codec->send(_conn,resp);
        }
    private:
        muduo::net::TcpConnectionPtr _conn;
        ProtobufCodecPtr _codec;
        ConsumerManager::ptr _cmp;
        VirtualHost::ptr _host;
        threadpool::ptr _pool;
        ChannelManager::ptr _channels;
    };

    class ConnectionManager{
    public:
        using ptr = std::shared_ptr<ConnectionManager>;

        ConnectionManager(){}

        void newConnection(const VirtualHost::ptr &host,
            const ConsumerManager::ptr &cmp,
            const ProtobufCodecPtr &codec,
            const muduo::net::TcpConnectionPtr &conn,
            const threadpool::ptr &pool){
            std::unique_lock<std::mutex> lock(_mutex);
            auto it = _conns.find(conn);
            if(it != _conns.end()){
                return;
            }
            Connection::ptr self_conn = std::make_shared<Connection>(host,cmp,codec,conn,pool);
            _conns.insert(std::make_pair(conn,self_conn));
        }

        void delConnection(const muduo::net::TcpConnectionPtr &conn){
            std::unique_lock<std::mutex> lock(_mutex);
            _conns.erase(conn);
        }

        Connection::ptr getConnection(const muduo::net::TcpConnectionPtr &conn){
            std::unique_lock<std::mutex> lock(_mutex);
            auto it = _conns.find(conn);
            if(it == _conns.end()){
                return Connection::ptr();
            }
            return it->second;
        }
    private:
        std::mutex _mutex;
        std::unordered_map<muduo::net::TcpConnectionPtr,Connection::ptr> _conns;
    };
}