#include "muduo/proto/dispatcher.h"
#include "muduo/proto/codec.h"
#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpClient.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/base/CountDownLatch.h"

#include "request.pb.h"
#include <iostream>

class Client{
public:
    typedef std::shared_ptr<google::protobuf::Message> MessagePtr;
    typedef std::shared_ptr<Fy::AddResponse> AddResponsePtr;
    typedef std::shared_ptr<Fy::TranslateResponse> TranslateResponsePtr;
    
    Client(const std::string &sip,int port)
        :_latch(1),_client(_loopthread.startLoop(),muduo::net::InetAddress(sip,port),"Client")
        ,_dispatcher(std::bind(&Client::onUnknowMessage,this,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3))
        ,_codec(std::bind(&ProtobufDispatcher::onProtobufMessage,&_dispatcher,
            std::placeholders::_1,std::placeholders::_2,std::placeholders::_3)){

            _dispatcher.registerMessageCallback<Fy::TranslateResponse>(std::bind(&Client::onTranslate,this,
            std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));

            _dispatcher.registerMessageCallback<Fy::AddResponse>(std::bind(&Client::onAdd,this,
            std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));

            _client.setMessageCallback(std::bind(&ProtobufCodec::onMessage,&_codec,
            std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));

            _client.setConnectionCallback(std::bind(&Client::onConnection,this,
            std::placeholders::_1));
        }
    
    void connect(){
        _client.connect();
        _latch.wait();
    }

    void Translate(const std::string &msg){
        Fy::TranslateRequest req;
        req.set_msg(msg);
        send(&req);
    }

    void Add(int num1,int num2){
        Fy::AddRequest req;
        req.set_num1(num1);
        req.set_num2(num2);
        send(&req);
    }
private:
    bool send(const google::protobuf::Message *message){
        if(_conn->connected()){
            _codec.send(_conn,*message);
            return true;
        }else{
            return false;
        }
    }

    void onTranslate(const muduo::net::TcpConnectionPtr& conn,const TranslateResponsePtr& message,muduo::Timestamp){
        std::cout<<"翻译结果:"<<message->msg()<<std::endl;
    }

    void onAdd(const muduo::net::TcpConnectionPtr& conn,const AddResponsePtr& message,muduo::Timestamp){
        std::cout<<"加法结果:"<<message->result()<<std::endl;
    }

    void onUnknowMessage(const muduo::net::TcpConnectionPtr& conn,const MessagePtr& message,muduo::Timestamp){
        LOG_INFO<<"onUnknowMessage:"<<message->GetTypeName();
        conn->shutdown();
    }

    void onConnection(const muduo::net::TcpConnectionPtr&conn){
        if(conn->connected()){
            _conn=conn;
            _latch.countDown();
        }else{
            _conn.reset();
        }
    }
private:
    muduo::CountDownLatch _latch;//实现同步，确保连接成功后才开始发送数据
    muduo::net::EventLoopThread _loopthread;//异步循环的事件处理器
    muduo::net::TcpConnectionPtr _conn;//客户端对应的连接
    muduo::net::TcpClient _client;
    ProtobufDispatcher _dispatcher;
    ProtobufCodec _codec;
};

int main(){
    Client client("127.0.0.1",8085);
    client.connect();

    client.Translate("hello");
    client.Add(11,22);

    sleep(1);
    return 0;
}