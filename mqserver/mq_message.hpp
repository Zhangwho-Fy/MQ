#ifndef __M_MSG_H__
#define __M_MSG_H__

#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_msg.pb.h"

#include <iostream>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <list>


namespace Fy_mq{
    #define DATAFILE_SUFFIX ".mqb"
    #define TMPFILE_SUFFIX ".mqb.tmp"

    using MessagePtr = std::shared_ptr<Fy_mq::Message>;
    
    class MessageMapper{
    public:
        MessageMapper(std::string &basedir,const std::string &qname)
        :_qname(qname){
            if(basedir.back() != '/') basedir.push_back('/');
            _datafile = basedir + qname + DATAFILE_SUFFIX;
            _tmpfile = basedir + qname + TMPFILE_SUFFIX;
            if(FileHelper(basedir).exists() == false){
                assert(FileHelper::createDirectory(basedir));
            }
            if(createMsgFile() == false){
                ELOG("创建队列消息管理文件%s失败!",_datafile.c_str());
            }
        }

        bool createMsgFile(){
            if(FileHelper(_datafile).exists() == true) return true;
            return FileHelper::createFile(_datafile);
        }

        void removeMsgFile(){
            FileHelper::removeFile(_datafile);
            FileHelper::removeFile(_tmpfile);
        }

        bool insert(MessagePtr &msg){
            return insert(_datafile,msg);
        }

        bool remove(MessagePtr &msg){
            // 1.将msg中有效标记位改为'0'
            msg->mutable_payload()->set_valid("0");
            // 2.对msg进行序列化
            std::string body = msg->payload().SerializeAsString();
            if(body.size() != msg->length()){
                DLOG("不能修改文件中的数据信息，因为新生成的数据长度和原数据长度不一致!");
                return false;
            }
            // 3.将序列化后的数据写入文件中
            FileHelper helper(_datafile);
            bool ret = helper.write(body.c_str(),msg->offset(),body.size());
            if (ret == false) {
                DLOG("向队列数据文件写入数据失败！");
                return false;
            }
            return true;
        }

        std::list<MessagePtr> gc(){
            //垃圾回收
            bool ret;
            std::list<MessagePtr> result;
            //1.先加载所有有效数据
            ret = load(result);
            if(ret == false){
                DLOG("加载有效数据失败！\n");
                return result;
            }
            //2.将有效数据序列化后存储到临时文件
            FileHelper::createFile(_tmpfile);
            for(auto &msg:result){
                DLOG("向临时文件写入数据: %s", msg->payload().body().c_str());
                ret = insert(_tmpfile,msg);
                if(ret == false){
                    DLOG("向临时文件写入数据失败!");
                    return result;
                }
            }
            //3.删除源文件
            ret = FileHelper::removeFile(_datafile);
            if(ret == false){
                DLOG("删除源文件失败!");
                return result;
            }
            //4.修改临时文件名为源文件名
            ret = FileHelper(_tmpfile).rename(_datafile);
            if(ret == false){
                DLOG("修改临时文件名失败!");
                return result;
            }

            return result;
        }

    private:
        bool load(std::list<MessagePtr> &result){
            // 1.加载出文件中的所有有效数据
            FileHelper data_file_helper(_datafile);
            size_t offset = 0,msg_size;
            size_t fsize = data_file_helper.size();
            bool ret;
            while(offset < fsize){
                ret = data_file_helper.read((char*)&msg_size,offset,sizeof(size_t));
                if(ret == false){
                    DLOG("读取消息长度失败!");
                    return false;
                }
                offset += sizeof(size_t);//记得移动偏移量
                std::string msg_body(msg_size,'\0');//初始化大小
                ret = data_file_helper.read(&msg_body[0],offset,msg_size);
                if(ret == false){
                    DLOG("读取消息数据失败!");
                    return false;
                }
                offset += msg_size;

                MessagePtr msgp = std::make_shared<Message>();
                msgp->mutable_payload()->ParseFromString(msg_body);
                if(msgp->payload().valid() == "0"){
                    DLOG("加载到无效消息：%s", msgp->payload().body().c_str());
                    continue;
                }
                result.push_back(msgp);
            }
            return true;
        }

        bool insert(const std::string &filename,MessagePtr &msg){
            //注，新增数据添加在文件末尾
            // 1.进行消息序列化处理，获取到格式化后的消息
            std::string body = msg->payload().SerializeAsString();//有效载荷序列化
            // 2.获取文件长度
            FileHelper helper(filename);
            size_t fsize = helper.size();
            size_t msg_size = body.size();
            // 写入：先写入4字节文件数据长度，再写入指定长度数据
            bool ret = helper.write((char *)&msg_size,fsize,sizeof(size_t));
            if(ret == false){
                DLOG("向队列数据文件:%s 写入数据长度失败!",filename.c_str());
                return false;
            }
            ret = helper.write(body.c_str(),fsize + sizeof(size_t), msg_size);
            if(ret == false){
                DLOG("向队列数据文件:%s 写入数据失败!",filename.c_str());
                return false;
            }
            // 3.更新msg中的实际存储位置
            msg->set_offset(fsize + sizeof(size_t));//不计报头哦
            msg->set_length(msg_size);
            return true;
        }

    private:
        std::string _qname;
        std::string _datafile;
        std::string _tmpfile;
    };
    
    class QueueMessage{
    public:
        using ptr = std::shared_ptr<QueueMessage>;

        QueueMessage(std::string &basedir, const std::string &qname)
        :_mapper(basedir,qname),_qname(qname),_valid_count(0),_total_count(0){}

        bool recovery(){
            //恢复历史数据
            std::unique_lock<std::mutex> lock(_mutex);
            _msgs = _mapper.gc();

            for(auto &msg : _msgs){
                //刚恢复的数据自然全都是持久化数据
                _durable_msgs.insert(std::make_pair(msg->payload().properties().id(),msg));
            }
            _valid_count = _total_count = _msgs.size();
            return true;
        }

        bool insert(const BasicProperties *bp,const std::string &body,bool queue_is_durable){
            // 1.构造消息对象
            MessagePtr msg = std::make_shared<Message>();
            msg->mutable_payload()->set_body(body);
            if(bp != nullptr){
                DeliverMode mode = queue_is_durable ? bp->deliver_mode() : DeliverMode::UNDURABLE;
                msg->mutable_payload()->mutable_properties()->set_id(bp->id());
                msg->mutable_payload()->mutable_properties()->set_deliver_mode(mode);
                msg->mutable_payload()->mutable_properties()->set_routing_key(bp->routing_key());
            }else{
                DeliverMode mode = queue_is_durable ? DeliverMode::DURABLE : DeliverMode::UNDURABLE;
                msg->mutable_payload()->mutable_properties()->set_id(UUIDHelper::uuid());
                msg->mutable_payload()->mutable_properties()->set_deliver_mode(mode);
                msg->mutable_payload()->mutable_properties()->set_routing_key("");
            }
            //上述部分为局部变量，无需加锁
            std::unique_lock<std::mutex> lock(_mutex);
            // 2.判断消息是否需要持久化
            if(msg->payload().properties().deliver_mode() == DeliverMode::DURABLE){
                msg->mutable_payload()->set_valid("1");//在持久化存储中表示数据有效
                // 3.进行持久化存储
                bool ret = _mapper.insert(msg);
                if( ret == false){
                    DLOG("持久化存储消息：%s 失败了！", body.c_str());
                    return false;
                }
                ++_valid_count;
                ++_total_count;
                _durable_msgs.insert(std::make_pair(msg->payload().properties().id(),msg));
            }
            // 4.添加至内存管理
            _msgs.push_back(msg);
            return true;
        }

        // 推送消息
        MessagePtr front(){
            std::unique_lock<std::mutex> lock(_mutex);
            if(_msgs.size() == 0){
                return MessagePtr();
            }

            MessagePtr msg = _msgs.front();
            _msgs.pop_front();
            _waitack_msgs.insert(std::make_pair(msg->payload().properties().id(),msg));
            return msg;
        }

        //每次删除消息后，判断是否需要垃圾回收
        bool remove(const std::string &msg_id){
            std::unique_lock<std::mutex> lock(_mutex);
            // 1.从待确认队列中查找消息
            auto it = _waitack_msgs.find(msg_id);
            if(it == _waitack_msgs.end()){
                DLOG("没有找到要删除的消息：%s!", msg_id.c_str());
                return true;
            }
            // 2.判断是否为持久化消息，是否删除
            if(it->second->payload().properties().deliver_mode() == DeliverMode::DURABLE){
                // 3.删除持久化消息
                _mapper.remove(it->second);
                _durable_msgs.erase(msg_id);
                --_valid_count;
                gc();
            }
            // 4.删除内存中的消息
            _waitack_msgs.erase(msg_id);
            return true;
        }

        size_t getable_count() {
            std::unique_lock<std::mutex> lock(_mutex);
            return _msgs.size();
        }

        size_t total_count() {
            std::unique_lock<std::mutex> lock(_mutex);
            return _total_count;
        }

        size_t durable_count() {
            std::unique_lock<std::mutex> lock(_mutex);
            return _durable_msgs.size();
        }

        size_t waitack_count() {
            std::unique_lock<std::mutex> lock(_mutex);
            return _waitack_msgs.size();
        }

        void clear() {
            std::unique_lock<std::mutex> lock(_mutex);
            _mapper.removeMsgFile();
            _msgs.clear();
            _durable_msgs.clear();
            _waitack_msgs.clear();
            _valid_count = 0;
            _total_count = 0;
        }
    private:
        bool GCCheck(){
            //如果持久化消息总量大于2000，且其中有效消息比例小于50%则需要垃圾回收
            if(_total_count > 2000 && 2 * _valid_count < _total_count){
                return true;
            }
            return false;
        }
        void gc(){
            if(GCCheck() == false)return;
            std::list<MessagePtr> msgs = _mapper.gc();
            for(auto &msg : msgs){
                auto it = _durable_msgs.find(msg->payload().properties().id());
                if (it == _durable_msgs.end()) {
                    DLOG("垃圾回收后，有一条持久化消息，在内存中没有进行管理!");
                    _msgs.push_back(msg); ///做法：重新添加到推送链表的末尾
                    _durable_msgs.insert(std::make_pair(msg->payload().properties().id(), msg));
                    continue;
                }
                // 更新每一条消息的实际存储位置
                it->second->set_offset(msg->offset());
                it->second->set_length(msg->length());
            }
            //更新当前有效消息数量 
            _valid_count = _total_count = msgs.size();
        }
    private:
        std::mutex _mutex;
        std::string _qname;
        size_t _valid_count;
        size_t _total_count;
        MessageMapper _mapper;
        std::list<MessagePtr> _msgs;
        std::unordered_map<std::string,MessagePtr> _durable_msgs;//持久化消息
        std::unordered_map<std::string,MessagePtr> _waitack_msgs;//待确认消息
    };

    class MessageManager{
    public:
        using ptr = std::shared_ptr<MessageManager>;
        
        MessageManager(const std::string &basedir):_basedir(basedir){}

        void clear(){
            std::unique_lock<std::mutex> lock(_mutex);
            for(auto &qmsg : _queue_msgs){
                qmsg.second->clear();
            }
        }

        void initQueueMessage(const std::string &qname){
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if(it != _queue_msgs.end()){
                    return;
                }
                qmp = std::make_shared<QueueMessage>(_basedir,qname);
                _queue_msgs.insert(std::make_pair(qname,qmp));
            }
            qmp->recovery();//使用内部锁
        }

        void destoryQueueMessage(const std::string &qname){
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if(it == _queue_msgs.end()){
                    return;
                }
                qmp = it->second;
                _queue_msgs.erase(it);
            }
            qmp->clear();
        }

        bool insert(const std::string &qname,BasicProperties *bp,const std::string &body,bool queue_is_durable){
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if(it == _queue_msgs.end()){
                    //主要可能是没有初始化
                    DLOG("向队列%s新增消息失败：没有找到消息管理句柄!", qname.c_str());
                    return false;
                }
                qmp = it->second;
            }
            return qmp->insert(bp,body,queue_is_durable);
        }

        MessagePtr front(const std::string &qname){
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if(it == _queue_msgs.end()){
                    //主要可能是没有初始化
                    DLOG("获取队列%s队首消息失败：没有找到消息管理句柄!", qname.c_str());
                    return MessagePtr();
                }
                qmp = it->second;
            }
            return qmp->front();
        }

        void ack(const std::string &qname,const std::string &msg_id){
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if(it == _queue_msgs.end()){
                    //主要可能是没有初始化
                        DLOG("确认队列%s消息%s失败：没有找到消息管理句柄!", qname.c_str(), msg_id.c_str());
                    return;
                }
                qmp = it->second;
            }
            qmp->remove(msg_id);
            return;
        }

         size_t getable_count(const std::string &qname) {
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if (it == _queue_msgs.end()) {
                    DLOG("获取队列%s待推送消息数量失败：没有找到消息管理句柄!", qname.c_str());
                    return 0;
                }
                qmp = it->second;
            }
            return qmp->getable_count();
        }

        size_t total_count(const std::string &qname) {
                QueueMessage::ptr qmp;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _queue_msgs.find(qname);
                    if (it == _queue_msgs.end()) {
                        DLOG("获取队列%s总持久化消息数量失败：没有找到消息管理句柄!", qname.c_str());
                        return 0;
                    }
                    qmp = it->second;
                }
                return qmp->total_count();
        }

        size_t durable_count(const std::string &qname) {
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if (it == _queue_msgs.end()) {
                    DLOG("获取队列%s有效持久化消息数量失败：没有找到消息管理句柄!", qname.c_str());
                    return 0;
                }
                qmp = it->second;
            }
            return qmp->durable_count();
        }

        size_t waitack_count(const std::string &qname) {
            QueueMessage::ptr qmp;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _queue_msgs.find(qname);
                if (it == _queue_msgs.end()) {
                    DLOG("获取队列%s待确认消息数量失败：没有找到消息管理句柄!", qname.c_str());
                    return 0;
                }
                qmp = it->second;
            }
            return qmp->waitack_count();
        }
    private:
        std::mutex _mutex;
        std::string _basedir;
        std::unordered_map<std::string,QueueMessage::ptr> _queue_msgs;
    };
}
#endif