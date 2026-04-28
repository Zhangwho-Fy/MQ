#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <atomic>

static std::string uuid() {
    static std::mt19937_64 generator(std::random_device{}()); // random_device机器随机数，效率低，当作种子，生成伪随机数
    std::uniform_int_distribution<int> dist(0, 255);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (int i = 0; i < 8; ++i) {
        ss << std::setw(2) << dist(generator);
        if (i == 3 || i == 5 || i == 7) {
            ss << '-';
        }
    }

    static std::atomic<size_t> seq(1);
    size_t num = seq.fetch_add(1);

    for (int i = 7; i >= 0; --i) {
        ss << std::setw(2) << ((num >> (i * 8)) & 0xFF);
        if (i == 6) ss << "-";
    }

    return ss.str();
}

int main(){
    for(int i = 0; i < 20; ++i){
        std::cout << uuid() << std::endl;
    }
    return 0;
}