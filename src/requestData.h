#pragma once
#include <string>
#include <unordered_map>
#include <memory>

const int STATE_PARSE_URI = 1;                  //分析请求行，初始化值
const int STATE_PARSE_HEADERS = 2;              //分析请求头
const int STATE_RECV_BODY = 3;                  //分析请求体,通常为POST请求才有
const int STATE_ANALYSIS = 4;                   //发送响应报文
const int STATE_FINISH = 5;                     //分析请求报文全部完成

const int MAX_BUFF = 4096;                      //指定每次接受数据的字节

// 有请求出现但是读不到数据,可能是Request Aborted,
// 或者来自网络的数据没有达到等原因,
// 对这样的请求尝试超过一定的次数就抛弃
const int AGAIN_MAX_TIMES = 200;                //读取不到网络数据限定的时间(read == 0)

const int PARSE_URI_AGAIN = -1;                 //找不到请求行的末尾
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;                

const int PARSE_HEADER_AGAIN = -1;              //重新开始分析请求头
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

const int EPOLL_WAIT_TIME = 500;                //长连接保持的时间

class MimeType
{
private:
    static pthread_mutex_t lock;
    static std::unordered_map<std::string, std::string> mime;   //static，只需赋值一次，后面共享一个拷贝
    MimeType();
    MimeType(const MimeType &m);
public:
    static std::string getMime(const std::string &suffix);
};

enum HeadersState
{
    h_start = 0,
    h_key,               //分析请求头中的key
    h_colon,
    h_spaces_after_colon,
    h_value,
    h_CR,               //将key和value关联起来
    h_LF,               //判断是否已经把整个请求头都分析完毕,未完成需跳回到h_key
    h_end_CR,           //判断请求头的格式是否正确
    h_end_LF            //到这步说明请求头已经顺利全部分析完毕，定位空行的开头pos，为后面做准备工作
};

struct mytimer;
class requestData;

class requestData : public std::enable_shared_from_this<requestData>
{
private:
    int againTimes;                                             //出现EAGAIN时开始计时，初始化为0
    std::string path;
    int fd;
    int epollfd;
    // content的内容用完就清
    std::string content;                                       //读取的内容
    int method;
    int HTTPversion;
    std::string file_name;                                    //客户请求的文件(GET请求)
    int now_read_pos;                                         //用于分析请求报文，定位每行/r的位置
    int state;                                                //分析整个请求报文时的状态
    int h_state;                                              //分析请求头时的状态
    bool isfinish;
    bool keep_alive;
    std::unordered_map<std::string, std::string> headers;
    std::weak_ptr<mytimer> timer;                             //指向时间戳

private:
    int parse_URI();                                          //分析请求行
    int parse_Headers();                                      //分析请求头
    int analysisRequest();                                    //发送响应报文

public:

    requestData();                                              //给lfd构造
    requestData(int _epollfd, int _fd, std::string _path);      //给clientfd构造用
    ~requestData();
    void addTimer(std::shared_ptr<mytimer> mtimer);
    void reset();
    void seperateTimer();
    int getFd();
    void setFd(int _fd);
    void handleRequest();                                        //对请求报文进行分析(控制执行parse_URI、parse_Headers、analysisRequest)
    void handleError(int fd, int err_num, std::string short_msg);   //出错时返回的响应报文
};

struct mytimer
{
    bool deleted;                                                   //记录是否在队列中被删除了
    size_t expired_time;
    std::shared_ptr<requestData> request_data;

    mytimer(std::shared_ptr<requestData> _request_data, int timeout);
    ~mytimer();
    void update(int timeout);   //重设超时时间，指定timeout
    bool isvalid();             //检查是否超时
    void clearReq();            //超时的话把request_data置为NULL
    void setDeleted();          //如果超时，需要deleted设置为true状态
    bool isDeleted() const;     //查看deleted状态
    size_t getExpTime() const; //查看超时的时间
};

struct timerCmp
{
    bool operator()(std::shared_ptr<mytimer> &a, std::shared_ptr<mytimer> &b) const;
};

//RAII,既避免繁琐的上锁解锁操作，又保证资源及时得到释放
class MutexLockGuard
{
public:
    explicit MutexLockGuard();
    ~MutexLockGuard();

private:
    static pthread_mutex_t lock;

private:
    MutexLockGuard(const MutexLockGuard&);
    MutexLockGuard& operator=(const MutexLockGuard&);
};