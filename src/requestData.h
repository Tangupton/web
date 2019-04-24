#pragma once
#include "timer.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <sys/epoll.h>


//#include <opencv/cv.h>
//#include <opencv2/core/core.hpp>
//#include <opencv2/highgui/highgui.hpp>
//#include <opencv2/opencv.hpp>
//using namespace cv;

const int STATE_PARSE_URI = 1;          //分析请求行，初始化值
const int STATE_PARSE_HEADERS = 2;      //分析请求头
const int STATE_RECV_BODY = 3;          //分析请求体,通常为POST请求才有
const int STATE_ANALYSIS = 4;           //发送响应报文
const int STATE_FINISH = 5;             //分析请求报文全部完成

const int MAX_BUFF = 4096;

// 有请求出现但是读不到数据,可能是Request Aborted,
// 或者来自网络的数据没有达到等原因,
// 对这样的请求尝试超过一定的次数就抛弃
const int AGAIN_MAX_TIMES = 200;        //读取不到网络数据限定的时间(read == 0)

const int PARSE_URI_AGAIN = -1;         //找不到请求行的末尾
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;      //重新开始分析请求头
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

const int EPOLL_WAIT_TIME = 500;        //长连接保持的时间

class MimeType
{
private:
    static void init();
    static std::unordered_map<std::string, std::string> mime;
    MimeType();
    MimeType(const MimeType &m);

public:
    static std::string getMime(const std::string &suffix);

private:
    //multiple getMime -> once
    static pthread_once_t once_control;       //实现线程安全的单例模式
};

//请求头状态机
enum HeadersState
{
    h_start = 0,
    h_key,                   //分析请求头中的key
    h_colon,
    h_spaces_after_colon,
    h_value,
    h_CR,                   //将key和value关联起来
    h_LF,                   //判断是否已经把整个请求头都分析完毕,未完成需跳回到h_key
    h_end_CR,               //判断请求头的格式是否正确
    h_end_LF                //到这步说明请求头已经顺利全部分析完毕，定位空行的开头pos，为后面做准备工作
};

class TimerNode;            //时间戳

//异步调用，保活。（完全摈弃普通指针，防止破坏智能指针的语义）
class RequestData : public std::enable_shared_from_this<RequestData>
{
private:
    std::string path;
    int fd;
    int epollfd;

    std::string inBuffer;
    std::string outBuffer;
    __uint32_t events;
    bool error;

    int method;
    int HTTPversion;          
    std::string file_name;
    int now_read_pos;
    int state;              //分析整个请求报文时的状态
    int h_state;            //分析请求头时的状态
    bool isfinish;
    bool keep_alive;
    std::unordered_map<std::string, std::string> headers;   

    //时间戳
    std::weak_ptr<TimerNode> timer;

    bool isAbleRead;        
    bool isAbleWrite;

private:
    int parse_URI();        //分析请求行
    int parse_Headers();    //分析请求头
    int analysisRequest();  //发送响应报文


public:

    RequestData();
    RequestData(int _epollfd, int _fd, std::string _path);  //给clientfd构造用
    ~RequestData();
    void linkTimer(std::shared_ptr<TimerNode> mtimer);      //关联其时间戳
    void reset();                                           //keep_alive
    void seperateTimer();
    int getFd();
    void setFd(int _fd);
    void handleRead();
    void handleWrite();
    void handleError(int fd, int err_num, std::string short_msg);   //出错时返回的响应报文
    void handleConn();

    void disableReadAndWrite();
    void enableRead();
    void enableWrite();
    bool canRead();
    bool canWrite();
};

