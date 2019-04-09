#ifndef REQUESTDATA
#define REQUESTDATA
#include <string>
#include <unordered_map>

const int STATE_PARSE_URI = 1;     //分析请求行，初始化值
const int STATE_PARSE_HEADERS = 2; //分析请求头
const int STATE_RECV_BODY = 3;    //分析请求体,通常为POST请求才有
const int STATE_ANALYSIS = 4;     //发送响应报文
const int STATE_FINISH = 5;      //分析请求报文全部完成

const int MAX_BUFF = 4096;       //指定每次接受数据的字节

const int AGAIN_MAX_TIMES = 200;  //读取不到网络数据限定的事件(read == 0)

const int PARSE_URI_AGAIN = -1;
const int PARSE_URI_ERROR = -2;      //不支持的URI
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;     //重新开始分析请求头
const int PARSE_HEADER_ERROR = -2;     //解析包头体时错误
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

const int EPOLL_WAIT_TIME = 500;  //长连接保持的时间

class MimeType
{
    private:
        static pthread_mutex_t lock;
        static std::unordered_map<std::string,std::string> mime;
        MimeType();
        MimeType(const MimeType &m);
    public:
        static std::string getMime(const std::string &suffix);
};

//分析请求头时的状态
enum HeadersState
{
    h_start = 0,
    h_key,                //分析请求头中的key
    h_colon,
    h_spaces_after_colon,
    h_value,
    h_CR,               //将key和value关联起来
    h_LF,               //判断是否已经把整个请求头都分析完毕,未完成需跳回到h_key
    h_end_CR,           //判断请求头的格式是否正确
    h_end_LF            //到这步说明请求头已经顺利全部分析完毕，定位空行的开头pos，为后面做准备工作
};

struct mytimer;
struct requestData;

struct requestData
{
    private:
        int againTimes;      //出现EAGAIN时开始计时，初始化为0
        std::string path;
        int fd;
        int epollfd;
        std::string content;    //读取的内容
        int method;
        int HTTPversion;
        std::string file_name; //客户请求的文件(GET请求)
        int now_read_pos;     //用于分析请求报文，定位每行/r的位置
        int state;            //分析整个请求报文时的状态
        int h_state;          //分析请求头时的状态
        bool isfinish;
        bool keep_alive;
        std::unordered_map<std::string,std::string> headers; //请求头，key-value
        mytimer* timer;

    private:
        int parse_URI();     //分析请求行
        int parse_Headers();    //分析请求头
        int analysisRequest();  //发送响应报文
    
    public:
        requestData();
        requestData(int _epollfd,int _fd,std::string _path);
        ~requestData();
        void addTimer(mytimer* mytimer);
        void reset();
        void seperateTimer();
        int getFd();
        void setFd(int _fd);
        void handleRequest();    //对请求报文进行分析(控制执行parse_URI、parse_Headers、analysisRequest)
        void handleError(int fd,int err_num,std::string short_msg);    //出错时返回的响应报文
};

struct mytimer
{
    bool deleted;
    size_t expired_time;
    requestData* request_data;

    mytimer(requestData* _request_data,int timeout);
    ~mytimer();
    void update(int timeout);
    bool isvalid();
    void clearReq();
    void setDeleted();
    bool isDeleted() const;
    size_t getExpTime() const;
};

struct timerCmp
{
    bool operator()(const mytimer* a,const mytimer* b) const;
};

#endif