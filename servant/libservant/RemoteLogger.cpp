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

#include "servant/RemoteLogger.h"
#include "servant/Communicator.h"
#include "servant/Application.h"
#include "servant/LogF.h"

namespace tars
{

int TimeWriteT::_dyeing = 0;

/////////////////////////////////////////////////////////////////////////////////////

RollWriteT::RollWriteT():_dyeingRollLogger(NULL), _maxSize(10000), _maxNum(1), _logPrx(NULL)
{
}

RollWriteT::~RollWriteT()
{
    if(_dyeingRollLogger)
    {
        delete _dyeingRollLogger;
    }
}

void RollWriteT::operator()(ostream &of, const deque<pair<size_t, string> > &ds)
{
    vector<string> vRemoteDyeing;

    deque<pair<size_t, string> >::const_iterator it = ds.begin();
    while(it != ds.end())
    {
        of << it->second;

        //染色线程id不存在
        if(it->first != 0)
        {
            if(!_dyeingRollLogger)
            {
                string sDyeingDir = _logPath;
                sDyeingDir += FILE_SEP;
                sDyeingDir += DYEING_DIR;
                sDyeingDir += FILE_SEP;

                string sDyeingFile = sDyeingDir;
                sDyeingFile += DYEING_FILE;

                TC_File::makeDirRecursive(sDyeingDir);

                //初始化染色循环日志
                _dyeingRollLogger = new TC_RollLogger();

                _dyeingRollLogger->init(sDyeingFile, _maxSize, _maxNum);
                _dyeingRollLogger->modFlag(TC_DayLogger::HAS_TIME, false);
                _dyeingRollLogger->modFlag(TC_DayLogger::HAS_TIME|TC_DayLogger::HAS_LEVEL|TC_DayLogger::HAS_PID, true);
                _dyeingRollLogger->setLogLevel("DEBUG");
            }

            _dyeingRollLogger->roll(make_pair(it->first, _app + "." + _server + "|" + it->second ));

            vRemoteDyeing.push_back(_app + "." + _server + "|" + it->second);
        }

        ++it;
    }
    of.flush();

    if(_logPrx && !vRemoteDyeing.empty())
    {
        try
        {

            _logPrx->logger(DYEING_DIR, DYEING_FILE, "roll", "%Y%m%d", vRemoteDyeing, _context);
        }
        catch(exception &ex)
        {
            TLOGERROR("[dyeing log write to remote log server error:" << ex.what() << "]" << endl);
        }
    }
}

void RollWriteT::setDyeingLogInfo(const string &sApp, const string &sServer, const string & sLogPath, int iMaxSize, int iMaxNum, const LogPrx &logPrx)
{
    _app     = sApp;
    _server  = sServer;
    _logPath = sLogPath;
    _maxSize = iMaxSize;
    _maxNum  = iMaxNum;
    _logPrx  = logPrx;

    if(_logPrx)
    {
        _context["node_name"] = _logPrx->tars_communicator()->getClientConfig().NodeName;
    }
}


/////////////////////////////////////////////////////////////////////////////////////

LocalRollLogger::~LocalRollLogger()
{
    terminate();

	for(const auto& e : _logger_ex)
	{
		delete e.second;
	}
	_logger_ex.clear();
}

void LocalRollLogger::terminate()
{
    if (!_terminate)
    {
        _terminate = true;
        _local.terminate();
    }
}

void LocalRollLogger::setLogInfo(const string &sApp, const string &sServer, const string &sLogpath, int iMaxSize, int iMaxNum, const CommunicatorPtr &comm, const string &sLogObj)
{
    if (_local.isStart())
    {
        return;
    }

    _app       = sApp;
    _server    = sServer;
    _logpath   = sLogpath;
	_logObj    = sLogObj;

	if(comm && !sLogObj.empty())
	{
		_logPrx = comm->stringToProxy<LogPrx>(sLogObj);
		//单独设置超时时间
		_logPrx->tars_timeout(3000);
	}

    //生成目录
    TC_File::makeDirRecursive(_logpath + FILE_SEP + _app + FILE_SEP + _server);

    _local.start();

    //初始化本地循环日志
    _logger.init(_logpath + FILE_SEP + _app + FILE_SEP + _server + FILE_SEP + _app + "." + _server, iMaxSize, iMaxNum);
    _logger.modFlag(TC_DayLogger::HAS_TIME, false);
    _logger.modFlag(TC_DayLogger::HAS_TIME|TC_DayLogger::HAS_LEVEL|TC_DayLogger::HAS_PID, true);

    //设置为异步
    sync(false);

    //设置染色日志信息
    _logger.getWriteT().setDyeingLogInfo(sApp, sServer, sLogpath, iMaxSize, iMaxNum, _logPrx);

}


void LocalRollLogger::sync(bool bSync)
{
    if(bSync)
    {
        _logger.unSetupThread();

		TC_ThreadRLock lock(_mutex);
		for(auto e : _logger_ex)
		{
			e.second->unSetupThread();
		}
	}
    else
    {
        _logger.setupThread(&_local);

		TC_ThreadRLock lock(_mutex);
		for(auto e : _logger_ex)
		{
			e.second->setupThread(&_local);
		}
    }
}

void LocalRollLogger::enableDyeing(bool bEnable, const string& sDyeingKey/* = ""*/)
{
    _logger.getRoll()->enableDyeing(bEnable, sDyeingKey);

	TC_ThreadRLock lock(_mutex);
	for(auto e : _logger_ex)
	{
		e.second->getRoll()->enableDyeing(bEnable, sDyeingKey);
	}
}

LocalRollLogger::RollLogger *LocalRollLogger::logger(const string &suffix)
{
	unordered_map<string, RollLogger*>::iterator it;

	{
		TC_ThreadRLock lock(_mutex);

		it = _logger_ex.find(suffix);

		if (it != _logger_ex.end())
		{
			return it->second;
		}
	}

	TC_ThreadWLock lock(_mutex);
	it = _logger_ex.find(suffix);

	if (it != _logger_ex.end())
	{
		return it->second;
	}

	RollLogger *logger = new RollLogger();

	//初始化本地循环日志
	logger->init(_logpath + FILE_SEP + _app + FILE_SEP + _server + FILE_SEP + _app + "." + _server + "." + suffix, _logger.getMaxSize(), _logger.getMaxNum());
	logger->modFlag(TC_DayLogger::HAS_TIME, false);
	logger->modFlag(TC_DayLogger::HAS_TIME|TC_DayLogger::HAS_LEVEL|TC_DayLogger::HAS_PID, true);

	//设置染色日志信息
	logger->getWriteT().setDyeingLogInfo(_app, _server, _logpath, _logger.getMaxSize(), _logger.getMaxSize(), _logPrx);

	_logger_ex[suffix] = logger;

	return logger;
}

/////////////////////////////////////////////////////////////////////////////////////

TarsLoggerThread::TarsLoggerThread()
{
    _local.start();
    _remote.start();
}

TarsLoggerThread::~TarsLoggerThread()
{
    terminate();
}

void TarsLoggerThread::terminate()
{
    if (!_terminate)
    {
        _terminate = true;

        //先刷新本地日志
        _local.flush();

        //再刷新远程日志, 保证不会丢日志
        _remote.flush();

        //终止
        _local.terminate();
        _remote.terminate();
    }
}

TC_LoggerThreadGroup* TarsLoggerThread::local()
{
    return &_local;
}

TC_LoggerThreadGroup* TarsLoggerThread::remote()
{
    return &_remote;
}

/////////////////////////////////////////////////////////////////////////////////////
RemoteTimeWriteT::RemoteTimeWriteT():_timeWrite(NULL)
{
}

RemoteTimeWriteT::~RemoteTimeWriteT()
{
}

void RemoteTimeWriteT::setTimeWriteT(TimeWriteT *pTimeWrite)
{
    _timeWrite = pTimeWrite;
}

void RemoteTimeWriteT::operator()(ostream &of, const deque<pair<size_t, string> > &buffer)
{
    const static uint32_t len = 2000;

    //写远程日志
    if(_timeWrite->_logPrx && !buffer.empty())
    {
        //大于50w条, 直接抛弃掉,否则容易导致内存泄漏
        if(buffer.size() > 500000)
        {
            _timeWrite->writeError(buffer);
            return;
        }

        vector<string> v;
        v.reserve(len);

        deque<pair<size_t, string> >::const_iterator it = buffer.begin();
        while(it != buffer.end())
        {
            v.push_back(it->second);

            ++it;

            //每次最多同步len条
            if(v.size() >= len)
            {
                sync2remote(v);
                v.clear();
                v.reserve(len);
            }
        }

        if(v.size() > 0)
        {
            sync2remote(v);
        }
    }
}

void RemoteTimeWriteT::sync2remote(const vector<string> &v)
{
    try
    {
        //此处传递set信息到远程logserver
        LogInfo stInfo;
        stInfo.appname           = _timeWrite->_app;
        stInfo.servername        = _timeWrite->_server;
        stInfo.sFilename         = _timeWrite->_file;
        stInfo.sFormat           = _timeWrite->_format;
        stInfo.setdivision       = _timeWrite->_setDivision;
        stInfo.bHasSufix         = _timeWrite->_hasSufix;
        stInfo.bHasAppNamePrefix = _timeWrite->_hasAppNamePrefix;
        stInfo.sConcatStr        = _timeWrite->_concatStr;
        stInfo.bHasSquareBracket = _timeWrite->_hasSquareBracket;
        stInfo.sSepar            = _timeWrite->_separ;
        stInfo.sLogType          = _timeWrite->_logType;

        _timeWrite->_logPrx->loggerbyInfo(stInfo,v, _timeWrite->_logPrx->tars_communicator()->getClientConfig().Context);

        if (_timeWrite->_reportSuccPtr)
        {
            _timeWrite->_reportSuccPtr->report(v.size());
        }
    }
    catch(exception &ex)
    {
        TLOGERROR("[write to remote log server error:" << ex.what() << ": buffer size:" << v.size() << "]"<< endl);
        _timeWrite->writeError(v);
        if (_timeWrite->_reportFailPtr)
        {
            _timeWrite->_reportFailPtr->report(v.size());
        }
    }
}

void RemoteTimeWriteT::sync2remoteDyeing(const vector<string> &v)
{
    try
    {
        _timeWrite->_logPrx->logger(DYEING_DIR, DYEING_FILE, "", _timeWrite->_format, v, _timeWrite->_logPrx->tars_communicator()->getClientConfig().Context);
    }
    catch(exception &ex)
    {
        TLOGERROR("[write dyeing log to remote log server error:" << ex.what() << ": buffer size:" << v.size() << "]" << endl);
        _timeWrite->writeError(v);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
//
TimeWriteT::~TimeWriteT()
{
    if(_remoteTimeLogger)
    {
        delete _remoteTimeLogger;
    }
}

TimeWriteT::TimeWriteT() : _remoteTimeLogger(NULL), _local(true), _remote(true), _dyeingTimeLogger(NULL),_setDivision(""),
    _hasSufix(true),_hasAppNamePrefix(true),_concatStr("_"),_separ("|"),_hasSquareBracket(false),_logType("")
{
}

void TimeWriteT::setLogInfo(const LogPrx &logPrx, const string &sApp, const string &sServer, const string &sFile, const string &sLogpath, const string &sFormat, const string& setdivision, const string& sLogType, const PropertyReportPtr &reportSuccPtr, const PropertyReportPtr &reportFailPtr)
{
    _logPrx      = logPrx;
    _app         = sApp;
    _server      = sServer;
    _format      = sFormat;
    _file        = sFile;
    _setDivision = setdivision;
    _logType     = sLogType;
    _reportSuccPtr = reportSuccPtr;
    _reportFailPtr = reportFailPtr;

    string sAppSrvName = _hasAppNamePrefix?(_app + "." + _server):"";

    _filePath = sLogpath + FILE_SEP + _app + FILE_SEP + _server + FILE_SEP + sAppSrvName;
    if(!_file.empty())
    {
        _filePath += (_hasAppNamePrefix?_concatStr:"") + sFile;
    }

    string sDyeingDir = sLogpath;
    sDyeingDir += FILE_SEP;
    sDyeingDir += DYEING_DIR;
    sDyeingDir += FILE_SEP;

    _dyeingFilePath = sDyeingDir;

    _remoteTimeLogger = new RemoteTimeLogger();
    _remoteTimeLogger->init(_filePath, _format,_hasSufix,_concatStr,NULL,true);
    _remoteTimeLogger->modFlag(0xffff, false);
    _remoteTimeLogger->setSeparator(_separ);
    _remoteTimeLogger->enableSqareWrapper(_hasSquareBracket);
    _remoteTimeLogger->setupThread(TarsLoggerThread::getInstance()->remote());
    _remoteTimeLogger->getWriteT().setTimeWriteT(this);

    if(!_local)
    {
        initError();
    }
}

void TimeWriteT::initDyeingLog()
{
    TC_File::makeDirRecursive(_dyeingFilePath);

    string sDyeingFile = _dyeingFilePath;
    sDyeingFile += FILE_SEP;
    sDyeingFile += DYEING_FILE;

    _dyeingTimeLogger = new DyeingTimeLogger();
    _dyeingTimeLogger->init(sDyeingFile, _format);
    _dyeingTimeLogger->modFlag(0xffff, false);
}

void TimeWriteT::setLogPrx(const LogPrx &logPrx)
{
    _logPrx     = logPrx;
}

void TimeWriteT::initError()
{
    //远程错误日志
    _logger.init(_filePath + ".remote.error", _format);
    _logger.modFlag(0xffff, false);
}

void TimeWriteT::enableLocal(bool bEnable)
{
    _local = bEnable;
    if(!_local)
    {
        initError();
    }
}

void TimeWriteT::operator()(ostream &of, const deque<pair<size_t, string> > &buffer)
{

    if(_local && of && !buffer.empty())
    {
        try
        {
            _wt(of, buffer);
        }
        catch(...)
        {
        }
    }

    if(_remote && _remoteTimeLogger && !buffer.empty())
    {
        deque<pair<size_t, string> >::const_iterator it = buffer.begin();
        while(it != buffer.end())
        {
            _remoteTimeLogger->any() << it->second;
            ++it;
        }
    }

    vector<string> vDyeingLog;
    deque<pair<size_t, string> >::const_iterator it = buffer.begin();
    while(it != buffer.end())
    {
        if(it->first != 0)
        {
            if(!_dyeingTimeLogger)
            {
                initDyeingLog();
            }
            _dyeingTimeLogger->any() << _app << "." << _server << "|" << it->second;

            vDyeingLog.push_back(_app + "." + _server + "|" + it->second);
        }
        ++it;
    }
    if(_logPrx && !vDyeingLog.empty())
    {
        try
        {
            _logPrx->logger(DYEING_DIR, DYEING_FILE, "day", "%Y%m%d", vDyeingLog, _logPrx->tars_communicator()->getClientConfig().Context);
        }
        catch(exception &ex)
        {
            TLOGERROR("[dyeing log write to remote log server error:" << ex.what() << "]" << endl);
        }
    }
}

void TimeWriteT::writeError(const vector<string> &buffer)
{
    if(!_local)
    {
        for(size_t i = 0; i < buffer.size(); i++)
        {
            _logger.any() << buffer[i];
        }
    }

    // //告警
    // string sInfo = _app + "." + _server + "|";
    // sInfo += ServerConfig::LocalIp + "|sync log to remote tarslog error";
    // FDLOG("tarserror") << sInfo <<endl;
    //TARS_NOTIFY_ERROR(sInfo);
}

void TimeWriteT::writeError(const deque<pair<size_t, string> > &buffer)
{
    if(!_local)
    {
        deque<pair<size_t, string> >::const_iterator it = buffer.begin();
        while(it != buffer.end())
        {
            _logger.any() << it->second;
            ++it;
        }
    }

    // //告警
    // string sInfo = _app + "." + _server + "|";
    // sInfo += ServerConfig::LocalIp + "|sync log to remote tarslog error(buffer.size>500000)";
    // FDLOG("tarserror") << sInfo <<endl;
    //TARS_NOTIFY_ERROR(sInfo);

}

/////////////////////////////////////////////////////////////////////////////////////

RemoteTimeLogger::RemoteTimeLogger() : _defaultLogger(NULL),_hasSufix(true),_hasAppNamePrefix(true),_concatStr("_"),_separ("|"),_hasSquareBracket(false),_local(true),_remote(true),_logStatReport(false)
{
}

RemoteTimeLogger::~RemoteTimeLogger()
{
    terminate();
}

void RemoteTimeLogger::terminate()
{
    if (!_terminate)
    {
        _terminate = true;
        if (_defaultLogger != NULL)
        {
            delete _defaultLogger;
        }

        auto it = _loggers.begin();
        while (it != _loggers.end())
        {
            delete it->second;
            ++it;
        }
        _loggers.clear();

    }
}

void RemoteTimeLogger::initTimeLogger(TimeLogger *pTimeLogger, const string &sFile, const string &sFormat,const LogTypePtr& logTypePtr)
{
	string sAppSrvName;
    string sFilePath;
    
    if(_app.empty() && _server.empty())
    {
        sAppSrvName = "";
        if(_logpath.empty())
        {
            sFilePath = sAppSrvName;
        }
        else
        {
            sFilePath = _logpath + FILE_SEP + sAppSrvName;
        }

        _hasAppNamePrefix = false;
    }
    else
    {
        sAppSrvName = _hasAppNamePrefix?(_app + "." + _server):"";
        sFilePath = _logpath + FILE_SEP + _app + FILE_SEP + _server + FILE_SEP + sAppSrvName;
    }
//    string sAppSrvName = _hasAppNamePrefix?(_app + "." + _server):"";
//    string sFilePath   = _logpath + "/" + _app + "/" + _server + "/" + sAppSrvName;

    if(!sFile.empty())
    {
        sFilePath += (_hasAppNamePrefix?_concatStr:"") + sFile;
    }

    //本地日志格式
    pTimeLogger->init(sFilePath, sFormat,_hasSufix,_concatStr,logTypePtr,!_local);
    pTimeLogger->modFlag(0xffff, false);
    pTimeLogger->modFlag(TC_DayLogger::HAS_TIME, true);
    pTimeLogger->setSeparator(_separ);
    pTimeLogger->enableSqareWrapper(_hasSquareBracket);
    pTimeLogger->setupThread(TarsLoggerThread::getInstance()->local());

    //远程日志格式
    pTimeLogger->getWriteT().enableSufix(_hasSufix);
    pTimeLogger->getWriteT().enablePrefix(_hasAppNamePrefix);
    pTimeLogger->getWriteT().setFileNameConcatStr(_concatStr);
    pTimeLogger->getWriteT().setSeparator(_separ);
    pTimeLogger->getWriteT().enableSqareWrapper(_hasSquareBracket);
    pTimeLogger->getWriteT().enableLocal(_local);
    pTimeLogger->getWriteT().enableRemote(_remote);

    string sLogType = "";
    if(logTypePtr)
    {
        sLogType = logTypePtr->toString();
    }
    
	////////////
    PropertyReportPtr reportSuccPtr = NULL;
    PropertyReportPtr reportFailPtr = NULL;
    if (_remote && _logStatReport && _comm)
    {
        string sKey   = sFile;
        reportSuccPtr = _comm->getStatReport()->createPropertyReport(sKey + "_log_send_succ", PropertyReport::sum());
        reportFailPtr = _comm->getStatReport()->createPropertyReport(sKey + "_log_send_fail", PropertyReport::sum());
    }

    pTimeLogger->getWriteT().setLogInfo(_logPrx, _app, _server, sFile, _logpath, sFormat, _setDivision, sLogType, reportSuccPtr, reportFailPtr);
}


void RemoteTimeLogger::initTimeLogger(TimeLogger *pTimeLogger,const string &sApp, const string &sServer, const string &sFile, const string &sFormat,const LogTypePtr& logTypePtr)
{

 //   string sAppSrvName = _hasAppNamePrefix?(sApp + "." + sServer):"";
 //   string sFilePath = _logpath + "/" + sApp + "/" + sServer + "/" + sAppSrvName;
	
	string sAppSrvName;
    string sFilePath;
    
    if(sApp.empty() && sServer.empty())
    {
        sAppSrvName = "";
        if(_logpath.empty())
        {
            sFilePath = sAppSrvName;
        }
        else
        {
            sFilePath = _logpath + FILE_SEP + sAppSrvName;
        }

        _hasAppNamePrefix = false;
    }
    else
    {
        sAppSrvName = _hasAppNamePrefix?(sApp + "." + sServer):"";
        sFilePath = _logpath + FILE_SEP + sApp + FILE_SEP + sServer + FILE_SEP + sAppSrvName;
    }

    if(!sFile.empty())
    {
        sFilePath += (_hasAppNamePrefix?_concatStr:"") + sFile;

    }

    //本地日志格式
    pTimeLogger->init(sFilePath,sFormat,_hasSufix,_concatStr,logTypePtr,!_local);
    pTimeLogger->modFlag(0xffff, false);
    pTimeLogger->modFlag(TC_DayLogger::HAS_TIME, true);
    pTimeLogger->setSeparator(_separ);
    pTimeLogger->enableSqareWrapper(_hasSquareBracket);
    pTimeLogger->setupThread(TarsLoggerThread::getInstance()->local());

    //远程日志格式
    pTimeLogger->getWriteT().enableSufix(_hasSufix);
    pTimeLogger->getWriteT().enablePrefix(_hasAppNamePrefix);
    pTimeLogger->getWriteT().setFileNameConcatStr(_concatStr);
    pTimeLogger->getWriteT().setSeparator(_separ);
    pTimeLogger->getWriteT().enableSqareWrapper(_hasSquareBracket);
    pTimeLogger->getWriteT().enableLocal(_local);
    pTimeLogger->getWriteT().enableRemote(_remote);
    string sLogType = "";
    if(logTypePtr)
    {
        sLogType = logTypePtr->toString();
    }

    PropertyReportPtr reportSuccPtr = NULL;
    PropertyReportPtr reportFailPtr = NULL;
    if (_remote && _logStatReport && _comm)
    {
        string sKey   = sFile;
        reportSuccPtr = _comm->getStatReport()->createPropertyReport(sKey + "_log_send_succ", PropertyReport::sum());
        reportFailPtr = _comm->getStatReport()->createPropertyReport(sKey + "_log_send_fail", PropertyReport::sum());
    }

    pTimeLogger->getWriteT().setLogInfo(_logPrx, sApp, sServer, sFile, _logpath, sFormat, _setDivision, sLogType, reportSuccPtr, reportFailPtr);
}

void RemoteTimeLogger::setLogInfo(const CommunicatorPtr &comm, const string &obj, const string &sApp, const string &sServer, const string &sLogpath, const string& setdivision, const bool &bLogStatReport)
{
    _app         = sApp;
    _server      = sServer;
    _logpath     = sLogpath;
    _comm        = comm;
    _setDivision = setdivision;
    _logStatReport = bLogStatReport;
    if(!obj.empty())
    {
        _logPrx = _comm->stringToProxy<LogPrx>(obj);
        //单独设置超时时间
        _logPrx->tars_timeout(3000);

        if(_defaultLogger)
        {
            _defaultLogger->getWriteT().setLogPrx(_logPrx);
        }
    }

    //创建本地目录
    TC_File::makeDirRecursive(_logpath + FILE_SEP + _app + FILE_SEP + _server);
}

void RemoteTimeLogger::initFormat(const string &sFile, const string &sFormat,const LogTypePtr& logTypePtr)
{
    if(sFile.empty())
    {
        if(!_defaultLogger)
        {
            _defaultLogger = new TimeLogger();

        }
        initTimeLogger(_defaultLogger, "", sFormat,logTypePtr);
    }
    else
    {
        string s = _app + FILE_SEP + _server + FILE_SEP + sFile;
        Lock lock(*this);
        auto it = _loggers.find(s);
        if( it == _loggers.end())
        {
            TimeLogger *p = new TimeLogger();
            initTimeLogger(p, sFile, sFormat,logTypePtr);
            _loggers[s] = p;
            return;
        }

        initTimeLogger(it->second, sFile, sFormat,logTypePtr);
    }
}
void RemoteTimeLogger::initFormat(const string &sApp, const string &sServer,const string &sFile, const string &sFormat,const LogTypePtr& logTypePtr)
{
    string s = sApp + FILE_SEP + sServer + FILE_SEP+ sFile;
    if (sFile.empty())
    {
        if (!_defaultLogger)
        {
            _defaultLogger = new TimeLogger();
            initTimeLogger(_defaultLogger, "", "%Y%m%d");
        }
    }
    else
    {
        Lock lock(*this);
        auto it = _loggers.find(s);
        if (it == _loggers.end())
        {
            TimeLogger *p = new TimeLogger();
            initTimeLogger(p, sApp, sServer, sFile, sFormat, logTypePtr);
            _loggers[s] = p;
            return;
        }

        initTimeLogger(it->second, sApp, sServer, sFile, sFormat, logTypePtr);
    }
}

RemoteTimeLogger::TimeLogger* RemoteTimeLogger::logger(const string &sFile)
{
    if(sFile.empty())
    {
        if(!_defaultLogger)
        {
            _defaultLogger = new TimeLogger();
            initTimeLogger(_defaultLogger, "", "%Y%m%d");
        }
        return _defaultLogger;
    }

    string s = _app + FILE_SEP + _server + FILE_SEP + sFile;
    Lock lock(*this);
    auto it = _loggers.find(s);
    if (it == _loggers.end())
    {
        TimeLogger *p = new TimeLogger();
        initTimeLogger(p, sFile, "%Y%m%d");
        _loggers[s] = p;
        return p;
    }

    return it->second;
}

RemoteTimeLogger::TimeLogger* RemoteTimeLogger::logger(const string &sApp, const string &sServer,const string &sFile)
{
    string s = sApp + FILE_SEP + sServer + FILE_SEP+ sFile;
    if (sFile.empty())
    {
        if (!_defaultLogger)
        {
            _defaultLogger = new TimeLogger();
            initTimeLogger(_defaultLogger, "", "%Y%m%d");
        }
        return _defaultLogger;
    }
    else
    {
        Lock lock(*this);
        auto it = _loggers.find(s);
        if (it == _loggers.end())
        {
            TimeLogger *p = new TimeLogger();
            initTimeLogger(p, sApp, sServer, sFile, "%Y%m%d");
            _loggers[s] = p;
            return p;
        }

        return it->second;
    }

}


void RemoteTimeLogger::sync(const string &sFile, bool bSync)
{
    if(bSync)
    {
        logger(sFile)->unSetupThread();
    }
    else
    {
        logger(sFile)->setupThread(TarsLoggerThread::getInstance()->local());
    }
}

void RemoteTimeLogger::enableRemote(const string &sFile, bool bEnable)
{
    logger(sFile)->getWriteT().enableRemote(bEnable);
}

void RemoteTimeLogger::enableRemoteEx(const string &sApp, const string &sServer,const string &sFile, bool bEnable)
{
    logger(sApp,sServer,sFile)->getWriteT().enableRemote(bEnable);
}
void RemoteTimeLogger::enableLocal(const string &sFile, bool bEnable)
{
    logger(sFile)->getWriteT().enableLocal(bEnable);
    logger(sFile)->setRemote(!bEnable);
}

void RemoteTimeLogger::enableLocalEx(const string &sApp, const string &sServer,const string &sFile, bool bEnable)
{
    logger(sApp,sServer,sFile)->getWriteT().enableLocal(bEnable);
    logger(sApp,sServer,sFile)->setRemote(!bEnable);
}

TarsDyeingSwitch::~TarsDyeingSwitch()
{
	if(_needDyeing)
	{
		LocalRollLogger::getInstance()->enableDyeing(false);

		ServantProxyThreadData * td = ServantProxyThreadData::getData();
		assert(NULL != td);
		if (td)
		{
			td->_data._dyeing = false;
			td->_data._dyeingKey = "";
		}
	}
}

bool TarsDyeingSwitch::getDyeingKey(string & sDyeingkey)
{
	ServantProxyThreadData * td = ServantProxyThreadData::getData();
	assert(NULL != td);

	if (td && td->_data._dyeing == true)
	{
		sDyeingkey = td->_data._dyeingKey;
		return true;
	}
	return false;
}

void TarsDyeingSwitch::enableDyeing(const string & sDyeingKey)
{
	LocalRollLogger::getInstance()->enableDyeing(true);

	ServantProxyThreadData * td = ServantProxyThreadData::getData();
	assert(NULL != td);
	if(td)

	{
		td->_data._dyeing    = true;
		td->_data._dyeingKey = sDyeingKey;
	}
	_needDyeing = true;
	_dyeingKey  = sDyeingKey;
}

}
