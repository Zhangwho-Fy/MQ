#include "include/muduo/net/TcpServer.h"
#include "include/muduo/net/EventLoop.h"
#include "include/muduo/net/TcpConnection.h"
#include <iostream>
#include <functional>
#include <string>
#include <unordered_map>

class TranslateServer
{
public:
    TranslateServer(int port)
        : _server(&_baseloop,
                  muduo::net::InetAddress("0.0.0.0", port),
                  "TranslateServer", muduo::net::TcpServer::kNoReusePort)
    {
        // 设置回调函数
        _server.setConnectionCallback(std::bind(&TranslateServer::onConnection, this, std::placeholders::_1));
        _server.setMessageCallback(std::bind(&TranslateServer::onMessage, this, std::placeholders::_1,
                                             std::placeholders::_2, std::placeholders::_3));
    }

    // 启动服务器
    void start()
    {
        _server.start();  // 开始事件监听
        _baseloop.loop(); // 开始事件监控
    }

private:
    void onConnection(const muduo::net::TcpConnectionPtr &conn)
    {
        // 建立连接成功时的回调函数
        if (conn->connected() == true)
        {
            std::cout << "新连接建立成功\n";
        }
        else
        {
            std::cout << "新连接关闭\n";
        }
    }

    std::string translate(const std::string &str)
    {
        static std::unordered_map<std::string, std::string> dict_map = {
            {"hello", "你好"},
            {"Hello", "你好"},
            {"你好", "Hello"},
            {"我爱你", "I love you"}};
        auto it = dict_map.find(str);
        if (it == dict_map.end())
        {
            return "不存在";
        }
        return it->second;
    }

    void onMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buf, muduo::Timestamp)
    {
        // 通信连接收到请求时的回调函数
        // 1.从buf中把请求的数据取出来
        std::string str = buf->retrieveAllAsString();
        // 2.调用translate接口进行翻译
        std::string resp = translate(str);
        // 3.对客户端进行响应结果
        conn->send(resp);
    }

private:
    muduo::net::EventLoop _baseloop;
    muduo::net::TcpServer _server;
};

int main()
{
    TranslateServer server(8085);
    server.start();
    return 0;
}