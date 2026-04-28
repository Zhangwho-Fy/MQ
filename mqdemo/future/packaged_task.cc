#include <iostream>
#include <future>
#include <thread>
#include <memory>

int Add(int num1,int num2){
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return num1+num2;
}

int main(){
    auto ptask = std::make_shared<std::packaged_task<int(int,int)>>(Add);
    std::future<int> fu = ptask->get_future();
    std::thread thr([ptask](){
        (*ptask)(11,22);
    });
    int sum = fu.get();
    std::cout<< sum <<std::endl;
    thr.join();
    return 0;
}