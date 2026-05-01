#ifndef __M_BINDING_H__
#define __M_BINDING_H__
#include "../mqcommon/mq_logger.hpp"
#include "../mqcommon/mq_helper.hpp"
#include "../mqcommon/mq_msg.pb.h"
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Fy_mq{
    struct Binding{
        using ptr = std::shared_ptr<Binding>;
        std::string exchange_name;
        std::string msgqueue_name;
        std::string binding_key;

        Binding() {};
        Binding(const std::string &ename,const std::string &qname,const std::string &key)
            :exchange_name(ename),msgqueue_name(qname),binding_key(key){}
    };

    //队列与绑定信息是一一对应的（因为是给某个交换机去绑定队列，因此一个交换机可能会有多个队列的绑定信息）
    //因此先定义一个队列名，与绑定信息的映射关系，这个是为了方便通过队列名查找绑定信息
    using MsgQueueBindMap = std::unordered_map<std::string,Binding::ptr>;
    using BindingMap = std::unordered_map<std::string,MsgQueueBindMap>;
    //std::unordered_map<std::string, Binding::ptr>;  队列与绑定  ，
    //std::unordered_map<std::string, Binding::ptr>;  交换机与绑定
    //采用上边两个结构，则删除交换机相关绑定信息的时候，不仅要删除交换机映射，还要删除对应队列中的映射，否则对象得不到释放
    class BindingMapper{
    public:
        BindingMapper(const std::string &dbfile): _sql_helper(dbfile){
            std::string path = FileHelper::parentDirectory(dbfile);
            FileHelper::createDirectory(path);
            _sql_helper.open();
            createTable();
        }

        void createTable(){
            std::stringstream sql;
            sql << "create table if not exists binding_table(";
            sql << "exchange_name varchar(32), ";
            sql << "msgqueue_name varchar(32), ";
            sql << "binding_key varchar(128),";
            sql << "primary key(exchange_name, msgqueue_name));";
            assert(_sql_helper.exec(sql.str(), nullptr, nullptr));
        }

        void removeTable(){
            std::string sql = "drop table if exists binding_table;";
            _sql_helper.exec(sql, nullptr, nullptr);
        }

        bool insert(Binding::ptr &binding){
            std::stringstream sql;
            sql << "insert into binding_table(exchange_name,msgqueue_name,binding_key) values(";
            sql << "'" << binding->exchange_name << "',";
            sql << "'" << binding->msgqueue_name << "',";
            sql << "'" << binding->binding_key << "');";
            return _sql_helper.exec(sql.str(),nullptr,nullptr);
        }

        void remove(const std::string &ename,const std::string &qname){
            std::stringstream sql;
            sql << "delete from binding_table where ";
            sql << "exchange_name = '" << ename << "' and ";
            sql << "msgqueue_name = '" << qname << "';";
            _sql_helper.exec(sql.str(),nullptr,nullptr);
        }

        void removeExchangeBindings(const std::string &ename){
            std::stringstream sql;
            sql << "delete from binding_table where ";
            sql << "exchange_name = '" << ename << "';";
            _sql_helper.exec(sql.str(),nullptr,nullptr);
        }

        void removeMsgqueueBindings(const std::string &qname){
            std::stringstream sql;
            sql << "delete from binding_table where ";
            sql << "msgqueue_name = '" << qname << "';";
            _sql_helper.exec(sql.str(),nullptr,nullptr);
        }

        BindingMap recovery() {
            BindingMap result;
            std::string sql = "select exchange_name , msgqueue_name , binding_key from binding_table;";
            _sql_helper.exec(sql,selectCallback,&result);
            return result;
        }
    private:
        static int selectCallback(void* arg,int numcol,char** row,char** fields){
            if (!row[0] || !row[1] || !row[2]) return 0;
            BindingMap *result = (BindingMap *)arg;
            auto& qmap = (*result)[row[0]];
            Binding::ptr bdp = std::make_shared<Binding>();
            bdp->exchange_name = row[0];
            bdp->msgqueue_name = row[1];
            bdp->binding_key = row[2];
            qmap.insert(std::make_pair(bdp->msgqueue_name,bdp));
            return 0;
        }
    private:
        SqliteHelper _sql_helper;
    };

    class BindingManager {
    public:
        using ptr = std::shared_ptr<BindingManager>;

        BindingManager(const std::string &dbfile):_mapper(dbfile){
            std::unique_lock<std::mutex> lock(_mutex);
            _bindings = _mapper.recovery();
        }
        
        bool bind(const std::string &ename,const std::string &qname,const std::string &key,bool durable){
            std::unique_lock<std::mutex> lock(_mutex);
            auto it = _bindings.find(ename);
            if(it != _bindings.end() && it->second.find(qname) != it->second.end()){
                return true;
            }
            Binding::ptr bp = std::make_shared<Binding>(ename,qname,key);
            if(durable){
                if( _mapper.insert(bp) == false )return false;
            }
            auto &qmap = _bindings[ename];
            qmap.insert(std::make_pair(qname,bp));
            return true;
        }

        void unBind(const std::string &ename,const std::string &qname){
            std::unique_lock<std::mutex> lock(_mutex);
            auto eit = _bindings.find(ename);
            if (eit == _bindings.end()) { return; }//没有交换机相关的绑定信息
            auto qit = eit->second.find(qname);
            if (qit == eit->second.end()) { return; }//交换机没有队列相关的绑定信息
            _mapper.remove(ename, qname);
            _bindings[ename].erase(qname);
        }

        void removeExchangeBindings(const std::string &ename){
            std::unique_lock<std::mutex> lock(_mutex);
            _mapper.removeExchangeBindings(ename);
            _bindings.erase(ename);
        }

        void removeMsgqueueBindings(const std::string &qname){
            std::unique_lock<std::mutex> lock(_mutex);
            _mapper.removeMsgqueueBindings(qname);
            for(auto&it :_bindings){
                it.second.erase(qname);
            }
        }

        MsgQueueBindMap getExchangeBindings(const std::string &ename){
            std::unique_lock<std::mutex> lock(_mutex);
            auto eit = _bindings.find(ename);
            if(eit == _bindings.end()){
                return MsgQueueBindMap();
            }
            return eit->second;
        }

        Binding::ptr getBinding(const std::string &ename, const std::string &qname){
            std::unique_lock<std::mutex> lock(_mutex);
            auto eit = _bindings.find(ename);
            if(eit == _bindings.end()){
                return Binding::ptr();
            }
            auto qit = eit->second.find(qname);
            if(qit == eit->second.end()){
                return Binding::ptr();
            }
            return qit->second;
        }

        bool exists(const std::string &ename, const std::string &qname) {
            std::unique_lock<std::mutex> lock(_mutex);
            auto eit = _bindings.find(ename);
            if (eit == _bindings.end()) { 
                return false; 
            }
            auto qit = eit->second.find(qname);
            if (qit == eit->second.end()) { 
                return false; 
            }
            return true;
        }

        size_t size() {
            size_t total_size = 0;
            std::unique_lock<std::mutex> lock(_mutex);
            for (auto start = _bindings.begin(); start != _bindings.end(); ++start) {
                total_size += start->second.size();
            }
            return total_size;
        }

        void clear() {
            std::unique_lock<std::mutex> lock(_mutex);
            _mapper.removeTable();
            _bindings.clear();
        }
        
    private:
        std::mutex _mutex;
        BindingMapper _mapper;
        BindingMap _bindings;
    };
}

#endif