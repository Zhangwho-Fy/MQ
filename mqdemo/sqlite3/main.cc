#include "sqlite.hpp"
#include <cassert>

int select_stu_callback(void* arg,int col_count,char** result,char** fields_name){
    std::vector<std::string> *array =(std::vector<std::string>*)arg;
    array->emplace_back(result[0]);//一次一行，一行一次调用
    return 0;//必须返回0
}

int main(){
    SqliteHelper helper("./test.db");
    // 1.创建/打开数据库
    assert(helper.open());

    // 2.创建表（不存在则创建），学生信息：学号，姓名，年龄
    const char *ct = "create table if not exists student(sn int primary key,name varchar(32),age int);";
    assert(helper.exec(ct,nullptr,nullptr));

    // 3.新增数据 删除 修改 查询

    // const char *insert_sql = "insert into student values(1,'张三',18),(2,'李四',19),(3,'王五',20);";
    // assert(helper.exec(insert_sql,nullptr,nullptr));

    // const char *update_sql = "update student set name='赵六' where sn=1;";
    // assert(helper.exec(update_sql,nullptr,nullptr));

    // const char *delete_sql = "delete from student where sn=3;";
    // assert(helper.exec(delete_sql,nullptr,nullptr));

    const char *select_sql = "select name from student;";
    std::vector<std::string> array;
    assert(helper.exec(select_sql,select_stu_callback,&array));
    for(auto &name:array){
        std::cout<<name<<std::endl;
    }
    // 4.关闭数据库
    helper.close();
    return 0;
}