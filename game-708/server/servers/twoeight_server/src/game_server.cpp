/*
* game_server.cpp
*
*  create on: 2019-8-12
*      Author:
*/
#include "framework/application.h"
#include "dbmysql_mgr.h"
#include "svrlib.h"
#include "stdafx.h" // for CGameSvrEventMgr
#include "game_server_config.h"
#include "async_dbcallback.h"
#include "data_cfg_mgr.h"
#include "msg_imple_handle.h"
#include "robot_oper_mgr.h" // for CRobotOperMgr


using namespace svrlib;
using namespace std;


bool CApplication::Initialize() {	
	defLuaConfig(getLua());
		
	// 加载lua 配置   
	if(this->call<bool>("game_config",m_uiServerID,&GameServerConfig::Instance()) == false) {
		LOG_ERROR("load game_config fail ");
		return false;
	}    
	CRobotOperMgr::Instance().Init();
	LOG_DEBUG("load config is:id:%d",m_uiServerID);
	if(CGameSvrEventMgr::Instance().GameServerInit() == false)
        return false;  

	CDBMysqlMgr::Instance().SetAsyncDBCallBack(new CGameAsyncDBCallBack());
	if(CLobbyMgr::Instance().Init(new CHandleImpleMsg()) == false) {
		LOG_ERROR("init lobbymgr fail");
		return false;
	}
	
	return true;
}

void  CApplication::ShutDown() {
	CRobotOperMgr::Instance().ShutDown();
    CGameSvrEventMgr::Instance().GameServerShutDown();
}

/**
* 本函数将在程序启动时和每次配置改变时被调用。
* 第一次调用将在Initialize()之前
*/
void CApplication::ConfigurationChanged() {
	// 重加载配置
	LOG_DEBUG("configuration changed");
    CDataCfgMgr::Instance().Reload();
}
void CApplication::Tick() {
    CGameSvrEventMgr::Instance().GameServerTick();
    CRobotOperMgr::Instance().OnTickRobot();
}

int main(int argc, char * argv[]) {
	return FrameworkMain(argc, argv);
}
