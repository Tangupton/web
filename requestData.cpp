#include "requestData.h"
#include "util.h"
#include "epoll.h"
#include <sys/epoll.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sys/time.h>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <queue>

//#include <opencv/cv.h>
//#include <opencv2/core/core.hpp>
//#include <opencv2/highgui/highgui.hpp>
//#include <opencv2/opencv.hpp>
//using namespace cv;

#include <iostream>
using namespace std;

pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  MimeType::lock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<std::string,std::string> MimeType::mime;

//suffix为 "GET /filename HTTP/1.1" 中的filename
std::string MimeType::getMime(const std::string &suffix)
{
    if(mime.size() == 0)
    {
        pthread_mutex_lock(&lock);
        if(mime.size() == 0)
        {
            mime[".html"] = "text/html";
            mime[".avi"] = "video/x-msvideo";
            mime[".bmp"] = "image.bmp";
            mime[".c"] = "text/plain";
            mime[".doc"] = "application/msword";
            mime[".gif"] = "image/gif";
            mime[".gz"] = "application/x-gzip";
            mime[".htm"] = "text/html";
            mime[".ico"] = "application/x-ico";
            mime[".jpg"] = "image/jpeg";
            mime[".png"] = "image/png";
            mime[".txt"] = "text/plain";
            mime[".mp3"] = "audio/mp3";
            mime["default"] = "text/html";
        }
        pthread_mutex_unlock(&lock);
    }
    if(mime.find(suffix) == mime.end())
        return mime["default"];
    else return mime[suffix];
}

priority_queue<mytimer*,deque<mytimer*>,timerCmp> myTimerQueue;

requestData::requestData():
    againTimes(0),now_read_pos(0),state(STATE_PARSE_URI),h_state(h_start),
    keep_alive(false),timer(NULL)
{
    cout<<"requestData constructed !"<<endl;
}

requestData::requestData(int _epollfd,int _fd,std::string _path):
    againTimes(0),path(_path),fd(_fd),epollfd(_epollfd),now_read_pos(0),
    state(STATE_PARSE_URI),h_state(h_start),keep_alive(false),timer(NULL) {}

requestData::~requestData()
{
    cout<<"~requestData()"<<endl;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.ptr = (void*)this;
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&ev);
    if(timer != NULL)
    {
        timer->clearReq();
        timer = NULL;
    }
    close(fd);
}

void requestData::addTimer(mytimer* mytimer)
{
    if(timer == NULL)
        timer = mytimer;
}

int requestData::getFd()
{
    return fd;
}

void requestData::setFd(int _fd)
{
    fd = _fd;
}

void requestData::reset()
{
    againTimes = 0;
    content.clear();
    file_name.clear();
    path.clear();
    now_read_pos = 0;
    state = STATE_PARSE_URI;
    h_state = h_start;
    headers.clear();
    keep_alive = false;
}

void requestData::seperateTimer()
{
    if(timer)
    {
        timer->clearReq();
        timer = NULL;
    }
}

void requestData::handleRequest()
{
    char buff[MAX_BUFF];
    bool isError = false;
    while(true)
    {
        int read_num = readn(fd,buff,MAX_BUFF);
        if(read_num < 0)
        {
            perror("1");
            isError = true;
            break;
        }
        else if(read_num == 0)
        {
            //有请求出现但是读不到数据，可能是request aborted，或者来自网络的数据没有达到等原因
            perror("read_num == 0");
            if(errno == EAGAIN)
            {
                if(againTimes > AGAIN_MAX_TIMES)
                    isError = true;
                else againTimes++;
            }
            else if(errno != 0)
                isError = true;
            break;
        }
        string now_read(buff,buff+read_num); //每次读到的内容
        content+=now_read;

        if(state == STATE_PARSE_URI)
        {
            int flag = this->parse_URI();
            if(flag == PARSE_URI_AGAIN)
            {
                break;
            }
            else if(flag == PARSE_URI_ERROR)
            {
                perror("2");
                isError = true;
                break;
            }
        }
        if(state == STATE_PARSE_HEADERS)
        {
            int flag = this->parse_Headers();
            if(flag == PARSE_HEADER_AGAIN)
            {
                break;
            }
            else if(flag == PARSE_HEADER_ERROR)
            {
                perror("3");
                isError = true;
                break;
            }
            //是否需要判断请求体
            if(method == METHOD_POST)
            {
                state = STATE_RECV_BODY;
            }
            else 
            {
                state = STATE_ANALYSIS;
            }
        }
        if(state == STATE_RECV_BODY)
        {
            int content_length = -1;
            if(headers.find("Content-length") != headers.end())
            {
                content_length = stoi(headers["Content-length"]);
            }
            else 
            {
                isError = true;
                break;
            }
            if(content.size() < content_length)
                continue;
            state = STATE_ANALYSIS;
        }
        if(state == STATE_ANALYSIS)
        {
            int flag = this->analysisRequest();
            if(flag < 0)
            {
                isError = true;
                break;
            }
            else if(flag == ANALYSIS_SUCCESS)
            {
                state = STATE_FINISH;
                break;
            }
            else
            {
                isError = true;
                break;
            }
        }
    }
    if(isError)
    {
        delete this;
        return;
    }
    if(state == STATE_FINISH)
    {
        if(keep_alive)
        {
            printf("ok\n");
            this->reset();
        }
        else
        {
            delete this;
            return;
        }
    }

    //新增时间信息
    pthread_mutex_lock(&qlock);
    mytimer* mtimer = new mytimer(this,500);
    timer = mtimer;
    myTimerQueue.push(mtimer);
    pthread_mutex_unlock(&qlock);

    __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
    int ret = epoll_mod(epollfd,fd,static_cast<void*>(this),_epo_event);
    if(ret < 0)
    {
        delete this;
        return;
    } 
}


int requestData::parse_URI()
{
    string &str = content;

    //定位到请求行的末尾
    int pos = str.find('\r',now_read_pos);
    if(pos < 0)
    {
        return PARSE_URI_AGAIN;
    }

    //提取请求行出来后去掉请求行，节省空间
    string request_line = str.substr(0,pos); 
    if(str.size() > pos+1)
        str = str.substr(pos+1);
    else str.clear();

    pos = request_line.find("GET");
    if(pos < 0)
    {
        pos = request_line.find("POST");
        if(pos < 0)
        {
            return PARSE_URI_ERROR;
        }
        else 
        {
            method = METHOD_POST;
        }
    }
    else 
    {
        method = METHOD_GET;
    }

    //此处的‘/’后面紧接着的是filename(如果有)
    pos = request_line.find("/",pos);
    if(pos < 0)
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        //_pos跑到filename(如果有)的后面
        int _pos = request_line.find(' ',pos);
        if(_pos < 0)
            return PARSE_URI_ERROR;
        else
        {
            if(_pos - pos > 1)
            {
                file_name = request_line.substr(pos+1,_pos-pos+1);
                int __pos = file_name.find('?');  //‘?’后面部分为参数，多个参数用‘&’分隔开
                if(__pos >= 0)
                {
                    file_name = file_name.substr(0,__pos);  //去掉参数部分的filename
                }
            }
            else 
                file_name = "index.html";   //默认请求的文件
        }
        pos = _pos;
    }

    //http版本号
    pos = request_line.find("/",pos);
    if(pos < 0)
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        if(request_line.size() - pos <= 3)
        {
            return PARSE_URI_ERROR;
        }
        else
        {
            string ver = request_line.substr(pos+1,3);
            if(ver == "1.0")
                HTTPversion = HTTP_10;
            else if(ver == "1.1")
                HTTPversion = HTTP_11;
            else return PARSE_URI_ERROR;
        }
    }
    //分析完请求行了，状态变成分析请求头
    state = STATE_PARSE_HEADERS;
    return PARSE_URI_SUCCESS;
}

int requestData::parse_Headers()
{
    string &str = content;      //注意：str是引用
    //请求头中key的范围[key_start,key_end]，value的范围[value_start,value_end全部初始化为0
    int key_start = -1,key_end = -1,value_start = -1,value_end = -1;
    int now_read_line_begin = 0;                //请求头中每行key-value的开头pos
    bool notFinish = true;
    for(size_t i = 0;i<str.size()&&notFinish;i++)
    {
        switch(h_state)
        {
            case h_start:
            { 
                if(str[i] == '\n'||str[i] == '\r')
                    break;
                h_state = h_key;
                key_start = i;
                now_read_line_begin = i;
                break;
            }
            case h_key:
            {
                if(str[i] == ':')    //key已经分析完毕，开始分析value
                {
                    key_end = i;
                    if(key_end - key_start <= 0)
                        return PARSE_HEADER_ERROR;
                    h_state = h_colon;
                }
                else if(str[i] == '\n'||str[i] == '\r')
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_colon:
            {
                if(str[i] == ' ')        //":"后面应该有一个空格，否则错误
                {
                    h_state = h_spaces_after_colon;
                }
                else 
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_spaces_after_colon:
            {
                h_state = h_value;
                value_start = i;
                break;
            }
            case h_value:
            {
                if(str[i] == '\r')          //value已经分析完毕,下一步需要将key和value关联起来
                {
                    h_state = h_CR;
                    value_end = i;
                    if(value_end - value_start <= 0)
                        return PARSE_HEADER_ERROR;
                }
                else if(i-value_start > 255) //key-value中的value不能超过255
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_CR:
            {
                if(str[i] == '\n')
                {
                    h_state = h_LF;
                    string key(str.begin()+key_start,str.begin()+key_end);
                    string value(str.begin()+value_start,str.begin()+value_end);
                    headers[key] = value;
                    now_read_line_begin = i;
                }
                else 
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_LF:
            {
                if(str[i] == '\r')          //整个请求头都分析完成了，跳下一步
                {
                    h_state = h_end_CR;
                }
                else                       //没分析完，调到h_key继续从key开始分析
                {
                    key_start = i;
                    h_state = h_key;
                }
                break;
            }
            case h_end_CR:               //\r后面理应紧接\n否则出错
            {
                if(str[i] == '\n')
                {
                    h_state = h_end_LF;
                }
                else 
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_end_LF:
            {
                notFinish = false;
                key_start = i;
                now_read_line_begin = i;    //空行的开头pos
                break;
            }
        }
    }
    if(h_state == h_end_LF)                     //如果分析请求头一切顺利
    {
        str = str.substr(now_read_line_begin);  //content的内容把请求头删除
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(now_read_line_begin);
    return PARSE_HEADER_AGAIN;
}

int requestData::analysisRequest()
{
    if(method == METHOD_POST)
    {
        char header[MAX_BUFF];
        sprintf(header,"HTTP/1.1 %d %s\r\n",200,"OK");
        //如果是长连接
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keep_alive = true;
            sprintf(header,"%sConnection: keep-alive\r\n",header);
            sprintf(header,"%sKeep-ALive: timeout=%d\r\n",header,EPOLL_WAIT_TIME);
        }
        //cout<<"content="<<content<<endl;
        char* send_content = "I have receiced this.";

        //%zu是size_t类型
        sprintf(header,"%sContent-length: %zu\r\n",header,strlen(send_content));
        sprintf(header,"%s\r\n",header);        //响应头完成
        size_t send_len = (size_t)writen(fd,header,strlen(header));
        if(send_len != strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }

        //发送响应体
        send_len = (size_t)writen(fd,send_content,strlen(send_content));
        if(send_len != strlen(send_content))
        {
            perror("Send content failed");
            return ANALYSIS_ERROR;
        }
        cout<<"content size == "<<content.size()<<endl;
        //vector<char> data(content.begin(),content.end());
        //Mat test = imdecode(data,CV_LOAD_IMAGE_ANYDEPTH|CV_LOAD_IMAGE_ANYCOLOR);
        //imwrite("receive.bmp",test);
        return ANALYSIS_SUCCESS;
    }
    else if(method == METHOD_GET)
    {
        char header[MAX_BUFF];
        sprintf(header,"HTTP/1.1 %d %s\r\n",200,"OK");
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keep_alive = true;
            sprintf(header,"%sConnection: keep-alive\r\n",header);
            sprintf(header,"%sKeep-ALive: timeout=%d\r\n",header,EPOLL_WAIT_TIME);
        }
        int dot_pos = file_name.find('.');
        const char* filetype;
        if(dot_pos < 0)
            filetype = MimeType::getMime("default").c_str();
        else
            filetype = MimeType::getMime(file_name.substr(dot_pos)).c_str();
        struct stat sbuf;
        if(stat(file_name.c_str(),&sbuf) < 0)
        {
            handleError(fd,404,"Not Found!");
            return ANALYSIS_ERROR;
        }

        sprintf(header,"%sContent-type: %s\r\n",header,filetype);
        sprintf(header,"%sContent-length: %ld\r\n",header,sbuf.st_size);
        sprintf(header,"%s\r\n",header);

        size_t send_len = (size_t)writen(fd,header,strlen(header));
        if(send_len != strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }
        int src_fd = open(file_name.c_str(),O_RDONLY,0);
        char* src_addr = static_cast<char*>(mmap(NULL,sbuf.st_size,PROT_READ,MAP_PRIVATE,src_fd,0));
        close(src_fd);

        send_len = writen(fd,src_addr,sbuf.st_size);
        if(send_len != sbuf.st_size)
        {
            perror("Send file failed");
            return ANALYSIS_ERROR;
        }
        munmap(src_addr,sbuf.st_size);
        return ANALYSIS_ERROR;
    }
    else 
        return ANALYSIS_ERROR;
}

void requestData::handleError(int fd,int err_num,string short_msg)
{
    short_msg = " "+short_msg;
    char send_buff[MAX_BUFF];
    string body_buff,header_buff;
    body_buff += "<html><title>Error</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(err_num) + short_msg;
    body_buff += "<hr><em>NO_tang's Web Server</em>\n</body></html>";

    header_buff += "HTTP/1.1"+to_string(err_num)+short_msg+"\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: "+to_string(body_buff.size())+"\r\n";
    header_buff += "\r\n";
    sprintf(send_buff,"%s",header_buff.c_str());
    writen(fd,send_buff,strlen(send_buff));
    sprintf(send_buff,"%s",body_buff.c_str());
    writen(fd,send_buff,strlen(send_buff));
}

mytimer::mytimer(requestData* _request_data,int timeout):deleted(false),request_data(_request_data)
{
    struct timeval now;
    gettimeofday(&now,NULL);
    //以毫秒计
    expired_time = ((now.tv_sec*1000)+(now.tv_usec/1000))+timeout;
}

mytimer::~mytimer()
{
    cout<<"~mytimer()"<<endl;
    if(request_data != NULL)
    {
        cout<<"request_data="<<request_data<<endl;
        delete request_data;
        request_data = NULL;
    }
}

void mytimer::update(int timeout)
{
    struct timeval now;
    gettimeofday(&now,NULL);
    expired_time = ((now.tv_sec*1000)+(now.tv_usec/1000))+timeout;
}

bool mytimer::isvalid()
{
    struct timeval now;
    gettimeofday(&now,NULL);
    size_t temp = ((now.tv_sec*1000)+(now.tv_usec/1000));
    if(temp < expired_time)
    {
        return true;
    }
    else 
    {
        this->setDeleted();
        return false;
    }
}

void mytimer::clearReq()
{
    request_data = NULL;
    this->setDeleted();
}

void mytimer::setDeleted()
{
    deleted = true;
}

bool mytimer::isDeleted() const
{
    return deleted;
}

size_t mytimer::getExpTime() const
{
    return expired_time;
}

bool timerCmp::operator()(const mytimer *a,const mytimer *b)const
{
    return a->getExpTime() > b->getExpTime();
}