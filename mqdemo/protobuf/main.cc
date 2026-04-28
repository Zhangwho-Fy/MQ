#include "contacts.pb.h"
#include <string>
#include <iostream>

int main()
{
    contacts::contact conn;
    conn.set_sn(202330);
    conn.set_name("张三");
    conn.set_score(81.5);

    // 持久化数据就放在str，这时候可以对str进行持久化或网络传输
    std::string str = conn.SerializeAsString();
    // 二进制存储
    std::cout << str << std::endl;

    contacts::contact stu;
    bool ret = stu.ParseFromString(str);
    if (!ret)
    {
        std::cout << "反序列化失败！" << std::endl;
        return -1;
    }
    std::cout << stu.sn() << std::endl;
    std::cout << stu.name() << std::endl;
    std::cout << stu.score() << std::endl;
    return 0;
}