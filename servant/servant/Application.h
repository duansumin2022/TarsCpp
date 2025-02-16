﻿/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#ifndef __TARS_APPLICATION_H
#define __TARS_APPLICATION_H

#include <iostream>
#include <set>
#include <signal.h>
#include <vector>

#include "util/tc_autoptr.h"
#include "util/tc_config.h"
#include "util/tc_option.h"
#include "util/tc_epoll_server.h"
#include "servant/BaseNotify.h"
#include "servant/ServantHelper.h"
#include "servant/ServantHandle.h"
#include "servant/Communicator.h"
#include "servant/StatReport.h"
#include "servant/RemoteLogger.h"
#include "servant/RemoteConfig.h"
#include "servant/RemoteNotify.h"
#include "servant/NotifyObserver.h"
#include "util/tc_openssl.h"

namespace tars
{

#define OUT_LINE        (TC_Common::outfill("", '-', 100))
#define OUT_LINE_LONG   (TC_Common::outfill("", '=', 100))
#define OUT_LINE_TAB(x) (TC_Common::outfill("", '-', 100 - 4*x))

/**
 * 以下定义配置框架支持的命令
 */
#define TARS_CMD_LOAD_CONFIG         "tars.loadconfig"        //从配置中心, 拉取配置下来: tars.loadconfig filename
#define TARS_CMD_SET_LOG_LEVEL       "tars.setloglevel"       //设置滚动日志的等级: tars.setloglevel [NONE, ERROR, WARN, DEBUG]
#define TARS_CMD_VIEW_STATUS         "tars.viewstatus"        //查看服务状态
#define TARS_CMD_VIEW_VERSION        "tars.viewversion"       //查看服务采用TARS的版本
#define TARS_CMD_CONNECTIONS         "tars.connection"        //查看当前链接情况
#define TARS_CMD_LOAD_PROPERTY       "tars.loadproperty"      //使配置文件的property信息生效
#define TARS_CMD_VIEW_ADMIN_COMMANDS "tars.help"              //帮助查看服务支持的管理命令
#define TARS_CMD_SET_DYEING          "tars.setdyeing"         //设置染色信息: tars.setdyeing key servant [interface]
#define TARS_CMD_CLOSE_COUT          "tars.closecout"         //设置是否启关闭cout\cin\cerr: tars.openthreadcontext yes/NO 服务重启才生效
#define TARS_CMD_SET_DAYLOG_LEVEL    "tars.enabledaylog"      //设置按天日志是否输出: tars.enabledaylog [remote|local]|[logname]|[true|false]
#define TARS_CMD_CLOSE_CORE          "tars.closecore"         //设置服务的core limit:  tars.setlimit [yes|no]
#define TARS_CMD_RELOAD_LOCATOR      "tars.reloadlocator"     //重新加载locator的配置信息
#define TARS_CMD_RESOURCE            "tars.resource"          //get resource
#define TARS_CMD_VIEW_BID            "tars.bid"               //查看服务编译时间,build id
//////////////////////////////////////////////////////////////////////
/**
 * 通知信息给notify服务, 展示在页面上
 */
//上报普通信息
#define TARS_NOTIFY_NORMAL(info)     {RemoteNotify::getInstance()->notify(NOTIFYNORMAL, info);}

//上报警告信息
#define TARS_NOTIFY_WARN(info)       {RemoteNotify::getInstance()->notify(NOTIFYWARN, info);}

//上报错误信息
#define TARS_NOTIFY_ERROR(info)      {RemoteNotify::getInstance()->notify(NOTIFYERROR, info);}

////发送心跳给node 多个adapter分别上报
//#define TARS_KEEPALIVE(adapter)      {KeepAliveNodeFHelper::getInstance()->keepAlive(adapter);}
//
////发送激活信息
//#define TARS_KEEPACTIVING            {KeepAliveNodeFHelper::getInstance()->keepActiving();}
//
////发送TARS版本给node
//#define TARS_REPORTVERSION(x)        {KeepAliveNodeFHelper::getInstance()->reportVersion(TARS_VERSION);}
//
//////////////////////////////////////////////////////////////////////
/**
 * 添加前置的命令处理方法
 * 在所有Normal方法之前执行
 * 多个前置方法之间顺序不确定
 */
#define TARS_ADD_ADMIN_CMD_PREFIX(c,f) \
    do { addAdminCommandPrefix(string(c), std::bind(&f, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); } while (0);

/**
 * 添加Normal命令处理方法
 * 在所有前置方法最后执行
 * 多个Normal方法之间顺序不确定
 */
#define TARS_ADD_ADMIN_CMD_NORMAL(c,f) \
    do { addAdminCommandNormal(string(c), std::bind(&f, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); } while (0);

//////////////////////////////////////////////////////////////////////

/**
 * 服务基本信息, 不使用的ServerConfig的原因是, 如果多个Application在同一个进程中初始化ServerConfig则会有问题
 */
struct SVT_DLL_API ServerBaseInfo
{
    std::string TarsPath;
    std::string Application;         //应用名称
    std::string ServerName;          //服务名称,一个服务名称含一个或多个服务标识
    std::string NodeName;            //服务如果部署在框架上, 则表示节点名称, 从模板中获取(framework>=3.0.17才支持), 否则为LocalIp
    std::string BasePath;            //应用程序路径，用于保存远程系统配置的本地目录
    std::string DataPath;            //应用程序数据路径用于保存普通数据文件
    std::string LocalIp;             //本机IP
    std::string LogPath;             //log路径
    int         LogSize;             //log大小(字节)
    int         LogNum;              //log个数()
    std::string LogLevel;            //log日志级别
    std::string Local;               //本地套接字
    std::string Node;                //本机node地址
    std::string Log;                 //日志中心地址
    std::string Config;              //配置中心地址
    std::string Notify;              //信息通知中心
    std::string ConfigFile;          //框架配置文件路径
    bool        CloseCout;
    int         ReportFlow;          //是否服务端上报所有接口stat流量 0不上报 1上报(用于非tars协议服务流量统计)
    int         IsCheckSet;          //是否对按照set规则调用进行合法性检查 0,不检查，1检查
    int         OpenCoroutine;       //是否启用协程处理方式(0~3)
    size_t      CoroutineMemSize;    //协程占用内存空间的最大大小
    uint32_t    CoroutineStackSize;  //每个协程的栈大小(默认128k)
    int         NetThread;           //servernet thread
    bool        ManualListen;        //是否启用手工端口监听
    int         BackPacketLimit;     //回包积压检查
    int         BackPacketMin;       //回包速度检查
    int         BakFlag = 0;
    int         BakType = 0;

    std::string CA;
    std::string Cert;
    std::string Key;
    bool VerifyClient;
    std::string Ciphers;
    map<string, string> Context;     //框架内部用, 传递节点名称(以域名形式部署时)
};

/**
 * 服务基本信息
 */
struct SVT_DLL_API ServerConfig
{
    static std::string TarsPath;
    static std::string Application;         //应用名称
    static std::string ServerName;          //服务名称,一个服务名称含一个或多个服务标识
    static std::string NodeName;            //服务如果部署在框架上, 则表示节点名称, 从模板中获取(framework>=3.0.17才支持), 否则为LocalIp
    static std::string BasePath;            //应用程序路径，用于保存远程系统配置的本地目录
    static std::string DataPath;            //应用程序数据路径用于保存普通数据文件
    static std::string LocalIp;             //本机IP
    static std::string LogPath;             //log路径
    static int         LogSize;             //log大小(字节)
    static int         LogNum;              //log个数()
    static std::string LogLevel;            //log日志级别
    static std::string Local;               //本地套接字
    static std::string Node;                //本机node地址
    static std::string Log;                 //日志中心地址
    static std::string Config;              //配置中心地址
    static std::string Notify;              //信息通知中心
    static std::string ConfigFile;          //框架配置文件路径
    static bool        CloseCout;
    static int         ReportFlow;          //是否服务端上报所有接口stat流量 0不上报 1上报(用于非tars协议服务流量统计)
    static int         IsCheckSet;          //是否对按照set规则调用进行合法性检查 0,不检查，1检查
    static int         OpenCoroutine;       //是否启用协程处理方式(0~3)
    static size_t      CoroutineMemSize;    //协程占用内存空间的最大大小
    static uint32_t    CoroutineStackSize;  //每个协程的栈大小(默认128k)
	static int         NetThread;           //servernet thread
	static bool        ManualListen;        //是否启用手工端口监听
	static int         BackPacketLimit;     //回包积压检查
	static int         BackPacketMin;       //回包速度检查
    static int         BakFlag;             //是否启用是备机: 0: 非备机, 1: 备机
    static int         BakType;             //主备切换类型: 0 不需要主备切换，1：自动主从切换， 2：自动切换但是不屏蔽主控路由
	static std::string CA;                  //ssl ca
	static std::string Cert;                //ssl 证书
	static std::string Key;                 //ssl 私钥
	static bool VerifyClient;               //是否验证客户端
	static std::string Ciphers;             //过滤的加密算法cd

	static map<string, string> Context;     //框架内部用, 传递节点名称(以域名形式部署时)

    /**
     * 转换成ServerBaseInfo, 在initializeServer之后会自动调用, 复制给Application中的_serverBaseInfo
     * @return
     */
    static ServerBaseInfo toServerBaseInfo();
};

class PropertyReport;
class KeepAliveNodeFHelper;

//////////////////////////////////////////////////////////////////////
/**
 * 服务的基类
 */
class Application: public BaseNotify
{
public:
    /**
     * 应用构造
     */
    Application();

    /**
     * 应用析构
     */
    virtual ~Application();

    /**
     * 初始化 --config=xxxx
     * @param argv
     */
    void main(int argc, char *argv[]);

    /**
     * init
     * @param option
     */
    void main(const TC_Option &option);

    /**
     * config , 实际配置文件的内容(而不是目录)
     * @param config
     */
	void main(const string &config);

	/**
	 * 运行
	 */
    void waitForShutdown();

    /**
     * 等待服务已经正常运行了
     */
    void waitForReady();

public:
    /**
     * 获取配置文件
     *
     * @return TC_Config&
     */
    TC_Config &getConfig() { return _conf; }
	const TC_Config &getConfig() const { return _conf; }

	/**
	 * 获取通信器
	 *
	 * @return CommunicatorPtr&
	 */
    static CommunicatorPtr& getCommunicator();

    /**
     * application自己的通信器, 当一个进程中内嵌多个Application时出现, 否则等于getCommunicator()
     * @return
     */
    CommunicatorPtr& getApplicationCommunicator();

    /**
     * 获取服务Server对象
     *
     * @return TC_EpollServerPtr&
     */
    TC_EpollServerPtr &getEpollServer() { return _epollServer; }

    /**
     * 获取服务server对象
     * @return
     */
	const TC_EpollServerPtr &getEpollServer() const { return _epollServer; }

	/**
	 *  中止应用
	 */
    void terminate();

    /**
     * 获取框架的版本
     */
    static string getTarsVersion();

    /**
     * 添加Config
     * @param filename
     */
    bool addConfig(const string &filename);

    /**
     * 添加应用级的Config
     * @param filename
     */
    bool addAppConfig(const string &filename);

    /**
     * 手工监听所有端口(Admin的端口是提前就监听的)
     */
    void manualListen();

    /**
     * 添加Servant
     * @param T
     * @param id
     */
    template<typename T>
    void addServant(const string &id)
    {
	    _servantHelper->addServant<T>(id, this, true);
    }

	/**
	 * 添加Servant
	 * @param T
	 * @param id
	 * @param p, p will pass to T, T must has constructor with p
	 */
	template<typename T, typename P>
	void addServantWithParams(const string &id, const P &p)
	{
		_servantHelper->addServant<T>(id, this, p, true);
	}

	/**
	 * get servant helper
	 * @return
	 */
	const shared_ptr<ServantHelperManager> &getServantHelper() { return _servantHelper; }

	/**
	 * get notify observer
	 * @return
	 */
	const shared_ptr<NotifyObserver> &getNotifyObserver() { return _notifyObserver; }

    /**
     * 非tars协议server，设置Servant的协议解析器
     * @param protocol
     * @param servant
     */
    void addServantProtocol(const string &servant, const TC_NetWorkBuffer::protocol_functor &protocol);

    /**
     * @desc 添加接收新链接的回调
     * 
     * @param cb
     */
    void addAcceptCallback(const TC_EpollServer::accept_callback_functor& cb);

    /**
     * 根据obj名称获取到BindAdpaterPtr
     * @param obj
     * @return 如果获取不到返回NULL
     */
    TC_EpollServer::BindAdapterPtr getBindAdapter(const string &obj);

    /**
     * 获取服务基本信息
     * @return
     */
    const ServerBaseInfo &getServerBaseInfo() const { return _serverBaseInfo; }

    /**
     * 是否已经结束了
     * @return
     */
    bool isTerminate();

protected:
    /**
     * 初始化, 只会进程调用一次
     */
    virtual void initialize() = 0;

    /**
     * 析够, 进程只会调用一次
     */
    virtual void destroyApp() {};

    /**
     * 解析服务的网络配置(业务可以在里面变更网络配置)
     */  
    virtual void onParseConfig(TC_Config &conf){};
    
    /**
     * 初始化ServerConfig之后, 给app调整自定义配置值的机会
     */    
    virtual void onServerConfig(){};

    /**
     * 成为主机(企业版功能)
     * 一主多备模式下, 切换成主机模式下回调给业务
     */
    virtual void onMaster() {};

    /**
     * 成为备机(企业版功能)
     * 一主多备模式下, 切换成备机模式下回调给业务
     */
    virtual void onSlave() {};

    /**
     * 处理加载配置的命令
     * 处理完成后继续通知Servant
     * @param filename
     */
    bool cmdLoadConfig(const string &command, const string &params, string &result);

    /**
     * 设置滚动日志等级
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdSetLogLevel(const string &command, const string &params, string &result);

    /**
     * 设置服务的core limit
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdCloseCoreDump(const string& command, const string& params, string& result);

    /**
     * 设置按天日志是否生效
     * @param command
     * @param params [remote|local]|[logname]|[true|false]
     * @param result
     *
     * @return bool
     */
    bool cmdEnableDayLog(const string &command, const string &params, string &result);

    /**
     * 查看服务状态
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdViewStatus(const string &command, const string &params, string &result);

    /**
     * 查看链接状态
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdConnections(const string &command, const string &params, string &result);

    /**
     * 查看编译的版本
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdViewVersion(const string &command, const string &params, string &result);

    /**
     * 查看服务的buildid（编译时间）
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdViewBuildID(const string &command, const string &params, string &result);

    /**
     * 使配置文件的property信息生效
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdLoadProperty(const string &command, const string &params, string &result);

    /**
     *查看服务支持的管理命令
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdViewAdminCommands(const string &command, const string &params, string &result);

    /**
     * 设置染色消息
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdSetDyeing(const string &command, const string &params, string &result);


    /**
     * 设置是否关闭stdcout/stderr/stdin 服务重启能才生效
     * @param command
     * @param params
     * @param result
     *
     * @return bool
     */
    bool cmdCloseCout(const string& command, const string& params, string& result);

    /*
    *通过命令动态加载配置文件，获取最新的locator，以方便应对主控便更
    * @param command
    * @param params
    * @param result
    */
    bool cmdReloadLocator(const string& command, const string& params, string& result);

	/*
	* view server resource
	* @param command
	* @param params
	* @param result
	*/
	bool cmdViewResource(const string& command, const string& params, string& result);

protected:

    /**
     * @desc callback when accept new client
     * 
     * @param cPtr
     */
    void onAccept(TC_EpollServer::Connection* cPtr);

    // /**
    //  *
    //  *
    //  * @param command
    //  * @param params
    //  * @param result
    //  *
    //  * @return bool
    //  */
    // void addServantOnClose(const string& servant, const TC_EpollServer::close_functor& f);

protected:
    /**
     * 读取基本信息
     */
    void initializeClient();

    /**
     * 输出
     * @param os
     */
    void outClient(ostream &os);

    /**
     * 初始化servant
     */
    void initializeServer();

    /**
     * 输出
     * @param os
     */
    void outServer(ostream &os);

    /**
     * 输出所有的adapter
     * @param os
     */
    void outAllAdapter(ostream &os);

    /**
     * 输出
     * @param os
     */
    void outAdapter(ostream &os, const string &v, TC_EpollServer::BindAdapterPtr lsPtr);

    /**
     * 解析配置文件
     */
	void parseConfig(const string &config);

	/**
	* 解析ip权限allow deny 次序
	*/
    TC_EpollServer::BindAdapter::EOrder parseOrder(const string &s);

    /**
     * 初始化adapter
     */
    void initializeAdapter();

    /**
     * 根据配置创建adapter
     * @return
     */
    vector<TC_EpollServer::BindAdapterPtr> createAdapter();

    /**
     * bind server adapter
     */
    void bindAdapters();

    /**
     * set adapter
     * @param adapter
     * @param name
     */
	void setAdapter(TC_EpollServer::BindAdapterPtr& adapter, const string &name);

	/**
     * @param servant
     * @param sPrefix
     */
    void checkServantNameValid(const string &servant, const string &sPrefix);

    /**
     * 换成缺省值
     * @param s
     *
     * @return string
     */
    string toDefault(const string &s, const string &sDefault);

    /**
     * 获取服务的set分组信息,setname.setarea.setgroup
     *
     * @return string 没有按set分组则返回空""
     */
    string setDivision(void);

    friend class ServantHandle;

private:
    /**
     * 检查主备状态(企业版框架才支持)
     */
    void checkMasterSlave(int timeout);

protected:

    /**
     * server base info
     */
    ServerBaseInfo _serverBaseInfo;

    /**
     * config
     */
    TC_Config _conf;

    /**
     * epoll server
     */
    TC_EpollServerPtr   _epollServer;

    /**
     * communicator
     */
    static CommunicatorPtr     _communicator;

    /**
     * communicator 每个application都自带一个, 如果_communicator已经存在, 则创建新的, 否则和_communicatory一样
     * 解决一个进程中内嵌多个Application时, 共享通信器的问题!
     */
    CommunicatorPtr     _applicationCommunicator;

    /**
     * accept
     */
    std::vector<TC_EpollServer::accept_callback_functor> _acceptFuncs;

    /**
     * servant helper
     */
    shared_ptr<ServantHelperManager>    _servantHelper;

    /**
     * notify observer
     */
    shared_ptr<NotifyObserver>          _notifyObserver;

    /**
     * ssl ctx
     */
	shared_ptr<TC_OpenSSL::CTX> 		_ctx = nullptr;

	size_t 								_ctrlCId = -1;
	size_t  							_termId = -1;

    shared_ptr<KeepAliveNodeFHelper>    _keepAliveNodeFHelper;

    shared_ptr<RemoteConfig>            _remoteConfig;

    TC_ThreadLock                       _masterSlaveLock;
    bool                                _terminateCheckMasterSlave = false;
    std::thread                         *_masterSlaveCheckThread = nullptr;        //主备模式检查线程(企业版框架功能)


};
////////////////////////////////////////////////////////////////////
}

#endif

