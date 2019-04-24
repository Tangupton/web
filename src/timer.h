#pragma once
#include "requestData.h"
#include "./base/nocopyable.hpp"
#include "./base/mutexLock.hpp"
#include <unistd.h>
#include <memory>
#include <queue>
#include <deque>

class RequestData;

class TimerNode
{
    typedef std::shared_ptr<RequestData> SP_ReqData;
private:
    bool deleted;                   //记录是否在队列中被删除了
    size_t expired_time;            //超时时间
    SP_ReqData request_data;        
public:
    TimerNode(SP_ReqData _request_data, int timeout);
    ~TimerNode();
    void update(int timeout);   //重设超时时间，指定timeout
    bool isvalid();             //检查是否超时
    void clearReq();            //超时的话把request_data置为NULL
    void setDeleted();          //如果超时，需要deleted设置为true状态
    bool isDeleted() const;     //查看deleted状态
    size_t getExpTime() const;  //查看超时的时间
};

struct timerCmp
{
    bool operator()(std::shared_ptr<TimerNode> &a, std::shared_ptr<TimerNode> &b) const
    {
        return a->getExpTime() > b->getExpTime();
    }
};

class TimerManager
{
    typedef std::shared_ptr<RequestData> SP_ReqData;
    typedef std::shared_ptr<TimerNode> SP_TimerNode;
private:
    std::priority_queue<SP_TimerNode, std::deque<SP_TimerNode>, timerCmp> TimerNodeQueue;
    MutexLock lock;
public:
    TimerManager();
    ~TimerManager();
    void addTimer(SP_ReqData request_data, int timeout);
    void addTimer(SP_TimerNode timer_node);
    void handle_expired_event();
};