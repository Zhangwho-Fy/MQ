#include "../mqcommon/mq_helper.hpp"


int main(){
    Fy_mq::FileHelper helper("../mqcommon/mq_logger.hpp");
    DLOG("是否存在:%d",helper.exists());
    DLOG("文件大小:%ld",helper.size());

    Fy_mq::FileHelper tmphelper("./aaa/bbb/ccc/tmp.hpp");
    if(tmphelper.exists() == false){
        std::string path = Fy_mq::FileHelper::parentDirectory("./aaa/bbb/ccc/tmp.hpp");
        if(Fy_mq::FileHelper(path).exists() == false){
            Fy_mq::FileHelper::createDirectory(path);
        }
        Fy_mq::FileHelper::createFile("./aaa/bbb/ccc/tmp.hpp");
    }

    std::string body;
    helper.read(body);
    tmphelper.write(body);

    tmphelper.write("12345678901",8,11);

    char str[16] = {0};
    tmphelper.read(str,8,11);
    DLOG("[%s]",str);

    tmphelper.rename("./aaa/bbb/ccc/test.hpp");
    Fy_mq::FileHelper::removeDirectory("./aaa");
    return 0;
}