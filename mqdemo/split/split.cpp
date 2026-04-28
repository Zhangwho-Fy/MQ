#include <iostream>
#include <string>
#include <vector>

class StrHelper{
public:
    static size_t split(const std::string &str, const std::string &sep, std::vector<std::string> &result){
        size_t pos,idx = 0,len = str.size(),seplen = sep.size();
        while(idx < len){
            pos = str.find(sep,idx);
            if(pos == std::string::npos){
                result.emplace_back(str.substr(idx));
                break;
            }else if(pos == idx){
                idx += seplen;
                continue;
            }else{
                result.emplace_back(str.substr(idx,pos-idx));
                idx = pos + seplen;
            }
        }
        return result.size();
    }
};

int main(){
    std::string str = "...news...music.#.pop...";
    std::vector<std::string> arry;
    int n = StrHelper::split(str,".",arry);
    for(auto &s:arry){
        std::cout<<s<<std::endl;
    }
    return 0;
}