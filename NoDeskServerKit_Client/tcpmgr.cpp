#include "tcpmgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include "TcpPacket.h"

//无参构造函数
TcpMgr::TcpMgr(QObject* parent, QString server_ip, unsigned short server_port)
    :QObject(parent),_socket(nullptr),_server_ip(server_ip),_server_port(server_port),_findflag_success(false),_recvhead_finish(false),sem(0)
{
    // 初始化连接服务器的socket对象状态
    qDebug()<<"tcp单例初始化所在线程："<<QThread::currentThread();
    // 单例首次调用创建，必定在主线程调用构造函数，因此单例对象和内部QObject成员均属于主线程
    // 通过在外部移动单例对象到子线程中，无法实现将内部在主线程创建并属于的QObject成员移动到子线程中
    // ①要么在构造初始化后，在内部将this移动到QThread成员内部，这样单例this本身和QObject成员通过父子关闭都移动到内部QThread子线程中。
    // ②要么在外部使用QThread非成员时，QObject成员不进行构造，而是在单例创建完被移动到外部QThread的子线程中构造。
    init_socket();

    // 将网络通信工作放入到子线程中完成
    networkThread = new QThread(this); // 初始化网络子线程
    // 将网络tcp的单例对象迁移到网络子线程中工作，与ui显示主线程分离
    this->moveToThread(networkThread);
    networkThread->start(); // 默认执行run函数开启工作子线程的事件循环

    //自定义信号和相应处理槽函数的连接
    //发送ChatServer服务器数据，TcpMgr发送tcp数据信号与给ChatServer服务器发送tcp数据的槽函数，通过信号槽机制实现发送数据会保证tcp发送数据的有序性
    connect(this,&TcpMgr::sig_tcp_send_data,this,&TcpMgr::slot_tcp_sendserver_data,Qt::QueuedConnection);

    //初始化逻辑层的map容器保存请求功能和处理函数
    init_map_reqid_callback();
}

//析构函数
TcpMgr::~TcpMgr(){
    // 释放没有指定父对象的socket资源，由于_socket是智能指针，因此不需要显示管理，自动释放
    // 等待网络子线程的退出
    if(networkThread->isRunning()){
        networkThread->quit(); // 请求子线程退出事件循环
        networkThread->wait(); // 等待子线程退出事件循环
        networkThread->deleteLater(); // 自动释放
    }
}

// 获取静态单例对象
std::shared_ptr<TcpMgr> TcpMgr::_instance = nullptr;
std::shared_ptr<TcpMgr> TcpMgr::get_instance(){
    static std::once_flag once_flag;
    std::call_once(once_flag,[](){
        TcpMgr::_instance.reset(new TcpMgr);
        // 构造后的单例移动到内部子线程对象中
        _instance->moveToThread(_instance->networkThread);
        qDebug()<<"单例所在线程："<<_instance->thread();
    });
    return _instance;
}

// 初始化连接服务器的socket对象状态
void TcpMgr::init_socket(){
    if (QThread::currentThread() != this->thread())
        qDebug() << "错误：initSocket 必须在TcpMgr对象网络子线程中调用";
    _socket = new QTcpSocket(this);
    //初始化信号槽机制，QTcpSocket类tcp连接的标准信号槽和自定义信号槽函数
    //tcp连接成功的标准信号
    connect(_socket,&QTcpSocket::connected,this,[this](){
        emit sig_tcp_connection_status(_server_ip,_server_port,ServerConnectStatus::STATUS_CONNECTED); // 链接服务器信号
        qDebug()<<"连接服务器成功，服务器的地址："<<_server_ip<<":"<<_server_port;
    });
    //tcp连接出现错误的标准信号
    connect(_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error) {
        emit sig_tcp_connection_status(_server_ip,_server_port,ServerConnectStatus::STATUS_CONNECT_ERROR); // 链接服务器信号
        qDebug() << "连接服务器失败，服务器的地址："<<_server_ip<<":"<<_server_port<<"，错误信息：" << error;
    });
    //tcp断开连接的标准信号
    connect(_socket,&QTcpSocket::disconnected,this,[this](){
        emit sig_tcp_connection_status(_server_ip,_server_port,ServerConnectStatus::STATUS_DISCONNECTED); // 链接服务器信号
        qDebug()<<"与服务器断开连接，服务器的地址："<<_server_ip<<":"<<_server_port;
    });

    //接收ChatServer服务器数据，tcp接收到服务器发送的数据在socket中的标准信号
    connect(_socket,&QTcpSocket::readyRead,this,&TcpMgr::slot_tcpserver_recvdata);
}

// 用于tcp长连接ChatServer服务器信号，进行tcp长连接ChatServer
void TcpMgr::slot_tcp_connectserver(QString ip, unsigned short port){
    qDebug()<<"连接tcp服务器的执行发出所在线程："<<QThread::currentThread();
    if(_socket==nullptr)
        init_socket();
    _server_ip = ip;
    _server_port = port;
    qDebug()<< "tcp长连接服务器，服务器地址："<<_server_ip<<":"<<_server_port;
    qDebug()<< "socket所在线程："<<_socket->thread();
    // 链接服务器
    if (_socket->state() != QAbstractSocket::ConnectedState)
        _socket->connectToHost(_server_ip, _server_port, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
    else {
        // 如果再次连接的服务器相同，无需重复连接
        if (_server_ip == ip && _server_port == port) {
            emit sig_tcp_connection_status(_server_ip,_server_port,ServerConnectStatus::STATUS_AGAIN_CONNECTED); // 链接服务器信号
            return;
        }

        // 连接一个新服务器，则断开现有连接后，连接新服务器
        slot_tcp_disconnectserver();
        if (_socket) {
            delete _socket;
            _socket = nullptr;
            QCoreApplication::processEvents();
        }

        // 保存服务器信息
        _server_ip = ip;
        _server_port = port;
        // 创建socket并初始化信号槽，重新连接服务器
        init_socket();
        qDebug() << "尝试连接TCP服务器：" << ip << ":" << port;
        _socket->connectToHost(_server_ip, _server_port, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
    }
}

// 用于tcp主动断开连接，但会发送缓冲区最后的数据
void TcpMgr::slot_tcp_disconnectserver(){
    // socket不为空并且是连接状态时
    if (_socket && (_socket->state() == QAbstractSocket::ConnectedState || _socket->state() == QAbstractSocket::ConnectingState)){
        _socket->disconnectFromHost(); // 优雅断开（推荐）,会发送最后缓冲区数据
        _socket->waitForDisconnected(50); // 等待1秒确保资源释放
        //断开所有旧信号槽的连接
        disconnect(_socket, 0, 0, 0);//断开所有信号
        delete _socket;
        _socket = nullptr; // 清空socket对象为nullptr
        QCoreApplication::processEvents();  // 立即处理pending事件
    }
}

// 接收服务器数据并进行tcp数据包格式处理
void TcpMgr::slot_tcpserver_recvdata(){
    _recv_buffer.append(_socket->readAll()); // 当有数据可读时读取所有数据，并追加到临时接收数据的缓冲区
    // 循环循环解析本次接收的数据，循环处理_recv_buffer接收的tcp数据，按照tcp数据包格式进行解析
    while(true) {
        /// 先查找数据的包开始标记FE FF，查找成功后创建数据流方式读取
        if(!_findflag_success){
            // 查找数据包头,未找到时直接丢弃所有接收的无效数据
            int pos = _recv_buffer.indexOf("\xFE\xFF");
            if(pos == -1){
                _recv_buffer.clear();
                qDebug()<<"未找到包开始标识：FE FF，数据已结束或数据错误。";
                return;
            }
            // 找到包头标记，丢弃前面pos + 2个无效数据
            _recv_buffer.remove(0, pos + 2);
            _findflag_success=true;
            // qDebug()<<"找到包开始标识：FE FF。";
        }

        /// 然后判断接收的完整消息长度的字节数据是否足够，没有则继续等待，否则接收并解析数据长度
        if(!_recvhead_finish){
            // 检查缓冲区中的数据是否足够解析出一个消息长度
            if (_recv_buffer.size() < DATA_LEN)
                return; // 数据不够，等待更多head数据
            // 缓冲区的数据长度可以解析，则转化为主机字节序的消息长度
            QDataStream stream(&_recv_buffer, QIODevice::ReadOnly);
            stream.setByteOrder(QDataStream::BigEndian);  // 设置为大端序（网络字节序）
            stream >> _message_len; // 自动按照主机字节序方式解析数据
            _recv_buffer = _recv_buffer.remove(0, DATA_LEN); //将_recv_buffer中的前四个字节移除
            _recvhead_finish = true;
            // qDebug() << "本次接收的完整消息数据(ID+消息数据本身+和校验位)的长度:" << _message_len;
        }

        /// 最终接收消息本身完整内容(ID+消息数据本身+和校验位)，剩余长度是否满足消息体长度，不满足则退出继续等待接受
        if(_recv_buffer.size() < _message_len)
            return; // 数据不够，等待更多context数据

        // _recv_buffer剩余长读满足消息体长度，继续读取完消息体本身
        _message_context = _recv_buffer.mid(0, _message_len); // 保存数据本身
        _recv_buffer = _recv_buffer.remove(0, _message_len);    // 接收数据缓冲区截取完已经被读取完的数据
        // qDebug() << "本次接收的ChatServer数据内容：" << _message_context;

        /// 处理本次接收的完整Tcp包数据，然后重置状态进行下个数据包接收
        deal_tcpserver_recvdata(_message_context, _message_len);
        _findflag_success = false;
        _recvhead_finish = false;
        _message_len = 0;
        _message_context.clear();
    }
}

// 处理接收的一个完整Tcp数据包数据，包括：消息ID+消息数据本身+和校验位
void TcpMgr::deal_tcpserver_recvdata(QByteArray data, int data_length){
    // 创建Tcp数据包，并检查有效性
    TcpPacket packet(data, data_length);
    if(!packet.is_valid()){
        qDebug()<<"接收的tcp数据包无效，不处理直接丢弃。";
        return;
    }

    // 有效tcp数据包，调用相应的回调函数处理
    std::map<DataId,std::function<void(QByteArray)>>::iterator it=_map_reqid_callback.find(packet.get_messageid());
    //没找到对应的处理函数，直接返回
    if(it==_map_reqid_callback.end()){
        qDebug()<<"接收的tcp数据包的消息ID无效，不处理直接丢弃。";
        return;
    }

    //找到就执行相应的处理函数处理数据
    it->second(QByteArray(packet.get_messagedata().get(), packet.get_messagelength()));
}

// 给tcp长连接的服务器发送数据，提供一个可以发送数据的公共接口通过信号给外界使用，按照tcp包协议格式封装数据后发送
void TcpMgr::slot_tcp_sendserver_data(DataId reqid, QByteArray data){
    // 检查是否链接服务器，未连接时进行提示
    if (!_socket||_socket->state() != QAbstractSocket::ConnectedState){
        emit sig_tcp_connection_status(_server_ip,_server_port,ServerConnectStatus::STATUS_TIP_DISCONNECTED); // 链接服务器信号
        return;
    }

    // 转化参数类型
    unsigned int length = data.size();
    std::shared_ptr<char[]> senddata(new char[length]);
    memcpy(senddata.get(), data.constData(), length);
    // 创建Tcp数据包
    TcpPacket packet(reqid, senddata, length);

    // 通过套接字socket，发送经过Tcp数据包格式封装后并转化为网络字节序的数据
    _socket->write(packet.get_senddata().get(), packet.get_senddatalen());
}


// 获取连接的服务器IP地址
QString TcpMgr::get_ipstr() const{
    return _server_ip;
}

// 获取连接的服务器IP地址
unsigned short TcpMgr::get_port() const{
    return _server_port;
}

// 获取服务器连接状态
int TcpMgr::get_connectstatus() const{
    int ret = 0; // 默认未连接
    if(_socket)
        ret = _socket->state();
    return ret;
}

// 用于与外界主线程进行同步
void TcpMgr::tcp_wait(){
    sem.acquire(); // 阻塞直到唤醒
}

void TcpMgr::tcp_signal(){
    sem.release(); // 唤醒
}


//构造函数内事先初始化map容器中接收的ChatServer数据和处理的回调函数
void TcpMgr::init_map_reqid_callback(){

    // 处理服务器回复的LOGIN_USER登录结果消息，用于通知登录主界面是否跳转信令控制界面
    _map_reqid_callback.insert(std::make_pair(DataId::LOGIN_USER,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的登录结果json数据为:"<<data;
        // 判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的服务器的登录结果数据有问题，无法解析的json数据！";
            return;
        }
        // 判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("username");
        if(!success){
            qDebug()<<"接收到的服务器登录结果数据不完整！";
            return;
        }

        // 解析登录结果：error为0表示登录成功，非0表示登录失败(具体类型见LoginStatus)
        int error=json_obj["error"].toInt();
        QString username=json_obj["username"].toString();
        LoginStatus status=static_cast<LoginStatus>(error);
        //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        emit sig_notice_login_result(status,username);
    }));

    // 处理服务器回复的GET_DRIVERINFO数据消息，通过信号方式发出发送获取的服务器磁盘分区信息结果在相应模块接收显示
    _map_reqid_callback.insert(std::make_pair(DataId::GET_DRIVERINFO,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收的服务器中磁盘分区信息json数据为:"<<data;
        // 判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的服务器的磁盘分区信息数据有问题，无法解析的json数据！";
            return;
        }
        // 判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("system")&&json_obj.contains("driverinfo");
        if(!success){
            qDebug()<<"接收到的服务器磁盘分区数据不完整！";
            return;
        }
        // 判断接收的磁盘分区信息是否有误
        if(json_obj["error"].toInt()){
            qDebug()<<"接收到的服务器磁盘分区数据被标记为错误数据！错误标识："<<json_obj["error"].toInt();
            return;
        }

        //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        QString server_system=json_obj["system"].toString();
        QString driverinfo=json_obj["driverinfo"].toString();
        emit sig_tip_notice_driverinfo(server_system, driverinfo);
    }));

    // 处理服务器的UPLOAD_FILE文件上传响应(进度/结果)，通过信号方式通知相应模块更新上传进度
    _map_reqid_callback.insert(std::make_pair(DataId::UPLOAD_FILE,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收的服务器文件上传响应json数据为:"<<data;
        // 判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的服务器的文件上传响应数据有问题，无法解析的json数据！";
            return;
        }
        // 判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("recvlen")
                    &&json_obj.contains("totallen")&&json_obj.contains("finishe")&&json_obj.contains("remotepath");
        if(!success){
            qDebug()<<"接收到的服务器文件上传响应数据不完整！";
            return;
        }

        // 解析上传响应字段
        int error=json_obj["error"].toInt(); // 错误号
        unsigned long long recvlen=json_obj["recvlen"].toInteger(); // 已接收字节数
        unsigned long long totallen=json_obj["totallen"].toInteger(); // 文件总长度
        bool finishe=json_obj["finishe"].toBool(); // 是否结束响应
        QString remotepath=json_obj["remotepath"].toString(); // 远程保存路径
        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        std::shared_ptr<UploadFileResp> resp=std::make_shared<UploadFileResp>(error,recvlen,totallen,finishe,remotepath);
        emit sig_notice_uploadfile_resp(resp);
        qDebug()<<QString("本次接收到文件:%1的上传响应，已接收:%2/%3。").arg(remotepath).arg(recvlen).arg(totallen);
    }));


    // 处理服务器的GET_DIRINFO数据消息进行显示，用于通知相应模块显示数据，通过信号方式发出发送结果
    _map_reqid_callback.insert(std::make_pair(DataId::GET_DIRINFO,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收的服务器中一条目录信息json数据为:"<<data;
        // 判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的服务器的目录信息数据有问题，无法解析的json数据！";
            return;
        }
        // 判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("parent_path")&&json_obj.contains("next");
        if(!success){
            qDebug()<<"接收到的服务器目录数据不完整！";
            return;
        }
        // 判断接收的磁盘分区信息是否有误
        if(json_obj["error"].toInt()){
            qDebug()<<"接收到的服务器目录数据被标记为错误数据！错误标识："<<json_obj["error"].toInt();
            return;
        }

        // 判断数据是否结束
        if(!json_obj["next"].toBool()){
            qDebug()<<"接收到的服务器目录数据完毕！";
            return;
        }

        // 数据无误，解析服务器发送的数据
        QString parent_path=json_obj["parent_path"].toString(); // 父目录路径
        QString file_name=json_obj["name"].toString(); // 文件名
        QString last_time=json_obj["access_time"].toString(); // 最后访问时间
        QString permissions=json_obj["permissions"].toString(); // 允许的权限操作
        QString type = json_obj["type"].toString(); // 普通文件、链接文件、其他类型文件
        unsigned long long size=json_obj["size"].toInteger(); // 大小
        QString symlink_target=json_obj["symlink_target"].toString(); // 链接文件的目标文件
        if(type=="file"||type=="other"){
            // 文件数据
            std::shared_ptr<FileInfo> fileinfo = std::make_shared<FileInfo>(parent_path,file_name,last_time,permissions,size,type,symlink_target);
            //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
            emit sig_tip_notice_fileinfo(fileinfo);
        }
        if(type=="directory"){
            // 目录数据
            std::shared_ptr<DirInfo> dirinfo = std::make_shared<DirInfo>(parent_path,file_name,last_time,permissions,size,type,symlink_target);
            //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
            emit sig_tip_notice_dirinfo(dirinfo);
        }
    }));

    // 处理服务器的DOWNLOAD_FILE数据消息进行创建子线程下载文件，用于通知相应模块显示数据，通过信号方式发出发送结果
    _map_reqid_callback.insert(std::make_pair(DataId::DOWNLOAD_FILE,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        // qDebug()<<"接收的服务器中一条文件块信息json数据";
        // 判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的服务器的文件信息数据有问题，无法解析的json数据！";
            return;
        }
        // 判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("sourcesavepath")&&json_obj.contains("destfilepath")
                    &&json_obj.contains("head")&&json_obj.contains("length")&&json_obj.contains("totallen")
                    &&json_obj.contains("next")&&json_obj.contains("index");
        if(!success){
            qDebug()<<"接收到的服务器文件数据不完整！";
            return;
        }

        // 接收到完整数据后的逻辑处理
        // 判断是否为文件头，并且判断该文件在服务器获取是否有错误
        int error = json_obj["error"].toInt(); // 错误号
        QString sourcesavepath=json_obj["sourcesavepath"].toString(); // 本地保存目录
        QString destfilepath=json_obj["destfilepath"].toString(); // 服务器被下载文件路径
        bool head=json_obj["head"].toBool(); // 文件头标记
        unsigned long long length=json_obj["length"].toInteger(); // 文件块长度
        unsigned long long totallen=json_obj["totallen"].toInteger(); // 文件总长度
        // 文件头出错时操作
        if(error==1&&head){
            // 有错误时发出错误信号进行提示
            qDebug()<<"接收到的服务器文件数据被标记为错误数据！错误标识："<<json_obj["error"].toInt();
            // 发出下载文件错误信息信号
            emit sig_notice_downloadfilehead_error(sourcesavepath,destfilepath,"文件不存在或没有权限.",false);;
            return;
        }
        // 文件头无错误时操作
        if(error==0&&head){
            // 无错误时发出初始化创建下载工作对象和进度条信号
            qDebug()<<"成功接收到服务器文件下载的头部数据！";
            emit sig_notice_downloadfilehead_success(sourcesavepath,destfilepath,totallen);
            // 阻塞等待外部主线程UI创建完毕后唤醒
            tcp_wait();
            return;
        }

        // 判断为文件块，通过信号将数据发送出去
        QString base64Data = json_obj["data"].toString(); // 文件块Base64编码的二进制数据
        // 将Base64编码的字符串数据转化回原二进制数据，通过Base64编码字符串可以实现二进制数据在json字符串中的转化无错误
        QByteArray filedata = QByteArray::fromBase64(base64Data.toUtf8());
        bool next=json_obj["next"].toBool(); // 是否还有下一个文件块
        int index = json_obj["index"].toInt(); // 文件块号
        // 通过信号发送数据，通知子线程的事件循环中下载该文件
        std::shared_ptr<DownLoadFileChunk> filechunk = std::make_shared<DownLoadFileChunk>(sourcesavepath,destfilepath,head,length,filedata,next,totallen,index);
        //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        emit sig_notice_downloadfilechunk(filechunk);
        qDebug()<<QString("本次成功接收的文件:%1的第%2个文件块。").arg(destfilepath).arg(index);
    }));

    // 处理EXECUTE_FILE服务器回复的执行文件数据消息进行提示，只在执行失败时进行提示
    _map_reqid_callback.insert(std::make_pair(DataId::EXECUTE_FILE,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的执行文件的Json数据:"<<data;
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的Json数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("executefilepath");
        if(!success){
            qDebug()<<"接收到的Json数据不完整！";
            return;
        }

        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        // 判断执行文件结果，只在执行文件失败时才通知
        int error=json_obj["error"].toInt();
        if(error!=0){
            QString executefilepath=json_obj["executefilepath"].toString();
            emit sig_notice_executefile_result(false,executefilepath);
        }
    }));

    // 处理DELETE_FILE服务器回复的删除文件数据消息进行提示，只在删除失败时进行提示
    _map_reqid_callback.insert(std::make_pair(DataId::DELETE_FILE,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的删除文件的Json数据:"<<data;
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的Json数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("deletefilepath");
        if(!success){
            qDebug()<<"接收到的Json数据不完整！";
            return;
        }

        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        // 判断删除文件结果，发出删除文件结果信号
        int error=json_obj["error"].toInt();
        QString deletefilepath=json_obj["deletefilepath"].toString();
        if(error!=0)
            emit sig_notice_deletefile_result(false,deletefilepath); // 删除文件失败
        else
            emit sig_notice_deletefile_result(true,deletefilepath); // 删除文件成功
    }));

    // 处理GET_REMOTEWATCH_SCREEN服务器回复的屏幕数据进行发送
    _map_reqid_callback.insert(std::make_pair(DataId::GET_REMOTEWATCH_SCREEN,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的远程监控屏幕的Json数据。";
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的Json数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error")&&json_obj.contains("screenpix");
        if(!success){
            qDebug()<<"接收到的Json数据不完整！";
            return;
        }

        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        // 判断接收到的屏幕数据结果，发出屏幕数据信号
        int error=json_obj["error"].toInt();
        if(error!=0){
            qDebug()<<"接收的屏幕数据有问题。";
        }
        else{
            // 将数据转化为图片发送出去
            // 获取Base64数据
            QString base64Str = json_obj["screenpix"].toString();
            // 转换回QPixmap
            QByteArray imageData = QByteArray::fromBase64(base64Str.toUtf8());
            QPixmap screenpix;
            screenpix.loadFromData(imageData);
            // 通过信号发送出去
            emit sig_notice_remotewatch_screenpix(screenpix);
        }
    }));

    // 处理JOIN_TCP_REMOTEWATCH服务器回复的加入远程监控消息进行提示
    _map_reqid_callback.insert(std::make_pair(DataId::JOIN_TCP_REMOTEWATCH,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的加入远程监控的Json数据:"<<data;
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的Json数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error");
        if(!success){
            qDebug()<<"接收到的Json数据不完整！";
            return;
        }

        // 解析服务端加入监控响应：error=0成功；error=1无桌面环境；error=2编码失败；其他系统错误
        int error=json_obj["error"].toInt();
        QString msg = json_obj.contains("msg") ? json_obj["msg"].toString() : "";
        RemoteWatchStatus status = static_cast<RemoteWatchStatus>(error);
        if(status == RemoteWatchStatus::WATCH_SUCCESS){
            qDebug()<<"加入远程监控成功，服务端已开启低延迟桌面RTSP流。";
            emit sig_notice_joinremotewatch_result(status, msg);
        }
        else{
            qDebug()<<"加入远程监控失败，错误码:"<<error<<"，信息:"<<msg;
            emit sig_notice_joinremotewatch_result(status, msg);
        }
    }));

    // 处理EXIT_TCP_REMOTEWATCH服务器回复的加入远程监控消息进行提示
    _map_reqid_callback.insert(std::make_pair(DataId::EXIT_TCP_REMOTEWATCH,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"接收到服务器回复的退出远程监控的Json数据:"<<data;
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的Json数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("error");
        if(!success){
            qDebug()<<"接收到的Json数据不完整！";
            return;
        }

        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        // 在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        // 判断接收到的屏幕数据结果，发出屏幕数据信号
        int error=json_obj["error"].toInt();
        if(error!=0)
            qDebug()<<"退出远程监控有问题。";
        else
            qDebug()<<"退出远程监控成功。";
    }));








    //处理ChatServer转发的SEND_MSG数据消息进行显示，用于转发对端好友对本身客户端发送消息进行通知，通过信号方式发出发送结果
    _map_reqid_callback.insert(std::make_pair(DataId::SEND_MSG,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        qDebug()<<"map中数据为:"<<data;
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的ChatServer数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("type")&&json_obj.contains("content")&&json_obj.contains("time")&&json_obj.contains("name");
        if(!success){
            qDebug()<<"接收到的ChatServer数据不完整！";
            return;
        }

        //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        QString type=json_obj["type"].toString();
        QString content=json_obj["content"].toString();
        QString name=json_obj["name"].toString();
        QString time=json_obj["time"].toString();
        std::shared_ptr<Message> message_ptr(new Message(name,content,time));

        qDebug()<<"成功接收到消息："<<content;
        emit sig_tip_notice_message(message_ptr);
    }));


    //处理ChatServer转发的SEND_File数据消息进行显示，用于转发对端好友对本身客户端发送消息进行通知，通过信号方式发出发送结果
    _map_reqid_callback.insert(std::make_pair(DataId::SEND_FILE,[this](QByteArray data){
        QJsonDocument json_doc=QJsonDocument::fromJson(data);
        //判断json_doc为空或无法转化为json对象
        if(json_doc.isNull()||!json_doc.isObject()) {
            qDebug()<<"接收到的ChatServer数据有问题！";
            return;
        }
        //判断接收的json数据是否完整
        QJsonObject json_obj=json_doc.object();
        bool success=json_obj.contains("type")&&json_obj.contains("content")&&json_obj.contains("filename")&&json_obj.contains("filesize")
                       &&json_obj.contains("filesuffix")&&json_obj.contains("time")&&json_obj.contains("name");
        if(!success){
            qDebug()<<"接收到的ChatServer数据不完整！";
            return;
        }

        //在确保数据接收无误并处理成功后，将接收的消息通过信号发送出去，以通过相应槽函数进行处理
        QString type=json_obj["type"].toString();
        QString name=json_obj["filename"].toString();
        qint64 size=json_obj["filesize"].toInteger();
        QString suffix=json_obj["filesuffix"].toString();
        QString user=json_obj["name"].toString();
        QString time=json_obj["time"].toString();
        QString content=json_obj["content"].toString();
        // 转换为QByteArray
        QByteArray base64Data = content.toUtf8();
        // 解码Base64
        QByteArray fileData = QByteArray::fromBase64(base64Data);

        std::shared_ptr<File> file_ptr(new File(user,name,size,suffix,fileData,time));

        qDebug()<<"成功接收到文件："<<name;
        emit sig_tip_notice_file(file_ptr);
    }));
}



