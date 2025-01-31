
#include <data_cfg_mgr.h>
#include <center_log.h>
#include "game_imple_table.h"
#include "stdafx.h"
#include "game_room.h"
#include "json/json.h"
#include "robot_mgr.h"
#include "common_logic.h"
#include "robot_oper_mgr.h"

using namespace std;
using namespace svrlib;
using namespace game_niuniu;
using namespace net;

namespace
{
    const static uint32 s_FreeTime           = 1 * 1000; // 空闲时间
	const static uint32 s_ReadyStartTime	 = 4 * 1000; // 开始倒计时
	const static uint32 s_GameStartTime		 = 2 * 1000; // 游戏开始
    const static uint32 s_ShowCardTime       = 4 * 100; // 发四张牌
	//const static uint32 s_ShowLastCardTime   = 4 * 100; // 发最后一张牌
	const static uint32 s_AddScoreTime		 = 6500;	 // 加注时间
	const static uint32 s_ApplyBrankerTime	 = 5 * 1000; // 申请庄家时间
	const static uint32 s_ChangeCardTime	 = 10 * 1000; // 摆牌时间
	const static uint32 s_GameEndTime		 = 12 * 1000; // 游戏结束时间

	const static int s_iArrApplyMultiple[NIU_MULTIPLE_COUNT] = { 0, 1, 2, 3, 4 };

};

CGameTable* CGameRoom::CreateTable(uint32 tableID)
{
    CGameTable* pTable = NULL;
    switch(m_roomCfg.roomType)
    {
    case emROOM_TYPE_COMMON:           // 普通房间
        {
            pTable = new CGameNiuniuTable(this,tableID,emTABLE_TYPE_SYSTEM);
        }break;
    case emROOM_TYPE_MATCH:            // 比赛房间
        {
            pTable = new CGameNiuniuTable(this,tableID,emTABLE_TYPE_SYSTEM);
        }break;
    case emROOM_TYPE_PRIVATE:          // 私人房间
        {
            pTable = new CGameNiuniuTable(this,tableID,emTABLE_TYPE_PLAYER);
        }break;
    default:
        {
            assert(false);
            return NULL;
        }break;
    }
    return pTable;
}
// 梭哈游戏桌子
CGameNiuniuTable::CGameNiuniuTable(CGameRoom* pRoom,uint32 tableID,uint8 tableType)
:CGameTable(pRoom,tableID,tableType)
{
    m_vecPlayers.clear();
	m_wBankerUser   =   INVALID_CHAIR;
    m_isNeedBanker  =   false;

	//用户状态
	ZeroMemory(m_cbPlayStatus,sizeof(m_cbPlayStatus));
	//扑克变量
	ZeroMemory(m_cbHandCardData,sizeof(m_cbHandCardData));

	ZeroMemory(m_lJettonMultiple, sizeof(m_lJettonMultiple));
	ZeroMemory(m_iApplyBankerPro, sizeof(m_iApplyBankerPro));

	ZeroMemory(m_iArrDispatchCardPro, sizeof(m_iArrDispatchCardPro));
	m_bIsNoviceWelfareCtrl = false;
	m_bIsProgressControlPalyer = false;
	m_bIsMasterUserOper = false;
	m_vecChangerChairID.clear();
	//下注信息
	ZeroMemory(m_lRobMultiple, sizeof(m_lRobMultiple));
	ZeroMemory(m_cbChangeCardData, sizeof(m_cbChangeCardData));

	ZeroMemory(m_lTableScore,sizeof(m_lTableScore));
	ZeroMemory(m_szShowCardState, sizeof(m_szShowCardState));
	ZeroMemory(m_cbTimeOutShowCard, sizeof(m_cbTimeOutShowCard));
    ZeroMemory(m_szNoOperCount,sizeof(m_szNoOperCount));
	ZeroMemory(m_szNoOperTrun, sizeof(m_szNoOperTrun));
	m_robotWinPro = 2000;
	m_tagControlPalyer.Init();
	m_vecRobotApplyBanker.clear();
	m_vecRobotJetton.clear();
	m_gameLogic.SetTable(this);
	m_coolRobot.beginCooling(g_RandGen.RandRange(2000, 3000));
	m_isNeedCheckStock = false; // add by har
}
CGameNiuniuTable::~CGameNiuniuTable()
{

}
bool    CGameNiuniuTable::CanEnterTable(CGamePlayer* pPlayer)
{
	if (pPlayer->GetTable() != NULL)
	{
		LOG_DEBUG("uid:%d,roomid:%d,tableid:%d,GetTable:%p",
			pPlayer->GetUID(), GetRoomID(), GetTableID(), pPlayer->GetTable());
		return false;
	}
    // 限额进入
    if(GetPlayerCurScore(pPlayer) < GetEnterMin() || GetChairPlayerNum() >= m_conf.seatNum)
	{
		LOG_DEBUG("uid:%d,roomid:%d,tableid:%d,curScore:%lld,GetEnterMin:%lld,GetChairPlayerNum:%d,seatNum:%d",
			pPlayer->GetUID(), GetRoomID(), GetTableID(), GetPlayerCurScore(pPlayer), GetEnterMin(), GetChairPlayerNum(), m_conf.seatNum);
        return false;
    }

	bool bIsNoviceWelfare = EnterNoviceWelfare(pPlayer);
	bool bIsKilledScore = false;// EnterAutoKillScore(pPlayer);
	uint32 freeCount = GetFreeChairNum();

	LOG_DEBUG("uid:%d,roomid:%d,tableid:%d,IsRobot:%d,freeCount:%d,bIsNoviceWelfare:%d,bIsKilledScore:%d",
		pPlayer->GetUID(), GetRoomID(), GetTableID(), pPlayer->IsRobot(), freeCount, bIsNoviceWelfare, bIsKilledScore);

	if (bIsNoviceWelfare == true || bIsKilledScore == true)
	{
		LOG_DEBUG("bIsNoviceWelfare is true or bIsKilledScore is true. uid:%d,roomid:%d,tableid:%d",
			pPlayer->GetUID(), GetRoomID(), GetTableID());
		return false;
	}
	if (CanEnterCtrledUserTable(pPlayer) == false)
	{
		LOG_DEBUG("CanEnterCtrledUserTable is false. uid:%d,roomid:%d,tableid:%d",
			pPlayer->GetUID(), GetRoomID(), GetTableID());
		return false;
	}
    return true;
}
bool    CGameNiuniuTable::CanLeaveTable(CGamePlayer* pPlayer)
{
	if (GetGameState() == TABLE_STATE_APBNIU_FREE)
	{
		return true;
	}
	if (GetGameState() == TABLE_STATE_APBNIU_READY_START)
	{
		return true;
	}
    if(GetGameState() != TABLE_STATE_APBNIU_FREE)
    {
        for(uint16 i=0;i<m_vecPlayers.size();++i)
		{
            if(m_vecPlayers[i].pPlayer == pPlayer)
            {
				if (m_cbPlayStatus[i] == TRUE)
				{
					return false;
				}
                break;
            }
        }
    }    
    return true;
}

void    CGameNiuniuTable::GetTableFaceInfo(net::table_face_info* pInfo)
{
    net::niuniu_table_info* pniuniu = pInfo->mutable_niuniu();
	pniuniu->set_tableid(GetTableID());
	pniuniu->set_tablename(m_conf.tableName);
    if(m_conf.passwd.length() > 1){
		pniuniu->set_is_passwd(1);
    }else{
		pniuniu->set_is_passwd(0);
    }
	pniuniu->set_hostname(m_conf.hostName);
	pniuniu->set_basescore(m_conf.baseScore);
	pniuniu->set_consume(m_conf.consume);
	pniuniu->set_entermin(m_conf.enterMin);
	pniuniu->set_duetime(m_conf.dueTime);
	pniuniu->set_feetype(m_conf.feeType);
	pniuniu->set_feevalue(m_conf.feeValue);
	pniuniu->set_card_time(s_AddScoreTime);
	pniuniu->set_table_state(GetGameState());
	pniuniu->set_seat_num(m_conf.seatNum);
	pniuniu->set_can_banker(m_isNeedBanker ? 1:0);
	pniuniu->set_apply_banker_time(s_ApplyBrankerTime);
	pniuniu->set_show_card_time(s_ChangeCardTime);
}

//配置桌子
bool    CGameNiuniuTable::Init()
{
    SetGameState(net::TABLE_STATE_APBNIU_FREE);
    m_vecPlayers.resize(GAME_PLAYER);
    for(uint8 i=0;i<GAME_PLAYER;++i)
    {
        m_vecPlayers[i].Reset();
    }

	m_isNeedBanker = true;

	m_lJettonMultiple[0] = 5;
	m_lJettonMultiple[1] = 10;
	m_lJettonMultiple[2] = 15;
	m_lJettonMultiple[3] = 20;
		
	m_iApplyBankerPro[0] = 1500;
	m_iApplyBankerPro[1] = 2500;
	m_iApplyBankerPro[2] = 3000;
	m_iApplyBankerPro[3] = 2500;
	m_iApplyBankerPro[4] = 500;

	for (int i = 1; i < CT_SPECIAL_MAX_TYPE; i++)
	{
		m_iArrDispatchCardPro[i] = 1000;
	}

	ReAnalysisParam();
	CRobotOperMgr::Instance().PushTable(this);
	SetMaxChairNum(GAME_PLAYER); // add by har
    return true;
}

bool CGameNiuniuTable::ReAnalysisParam()
{
	string param = m_pHostRoom->GetCfgParam();
	Json::Reader reader;
	Json::Value  jvalue;
	LOG_DEBUG("reader_start - roomid:%d,tableid:%d,param:%s", GetRoomID(), GetTableID(), param.c_str());
	if (!reader.parse(param, jvalue))
	{
		LOG_ERROR("reader json parse error - roomid:%d,tableid:%d,param:%s", GetRoomID(), GetTableID(), param.c_str());
		return true;
	}
	string strJettonMultiple;
	for (int i = 0; i < JETTON_MULTIPLE_COUNT; i++)
	{		
		strJettonMultiple += CStringUtility::FormatToString("i:%d-m:%d ", i, m_lJettonMultiple[i]);
	}

	string strRobotJettonPro;
	for (int i = 0; i < JETTON_MULTIPLE_COUNT; i++)
	{
		string strPro = CStringUtility::FormatToString("jp%d", i);
		if (jvalue.isMember(strPro.c_str()) && jvalue[strPro.c_str()].isIntegral())
		{
			m_iRobotJettonPro[i] = jvalue[strPro.c_str()].asInt64();
		}
		strRobotJettonPro += CStringUtility::FormatToString("i:%d-p:%d ", i, m_iRobotJettonPro[i]);
	}

	string strApplyBankerPro;
	for (int i = 0; i < NIU_MULTIPLE_COUNT; i++)
	{
		string strPro = CStringUtility::FormatToString("bp%d", i);
		if (jvalue.isMember(strPro.c_str()) && jvalue[strPro.c_str()].isIntegral())
		{
			m_iApplyBankerPro[i] = jvalue[strPro.c_str()].asInt();
		}
		strApplyBankerPro += CStringUtility::FormatToString("i:%d-p:%d ", i, m_iApplyBankerPro[i]);
	}

	string strArrDispatchCardPro;
	for (int i = 0; i < CT_SPECIAL_MAX_TYPE; i++)
	{
		string strPro = CStringUtility::FormatToString("p%02d", i);
		if (jvalue.isMember(strPro.c_str()) && jvalue[strPro.c_str()].isIntegral())
		{
			m_iArrDispatchCardPro[i] = jvalue[strPro.c_str()].asInt();
		}
		strArrDispatchCardPro += CStringUtility::FormatToString("i:%d-p:%d ", i, m_iArrDispatchCardPro[i]);
	}

	if (jvalue.isMember("rwp") && jvalue["rwp"].isIntegral())
	{
		m_robotWinPro = jvalue["rwp"].asInt();
	}

	LOG_DEBUG("json_success - roomid:%d,tableid:%d,m_robotWinPro:%d,strJettonMultiple:%s,strApplyBankerPro:%s,strRobotJettonPro:%s,strArrDispatchCardPro:%s",
		GetRoomID(), GetTableID(), m_robotWinPro, strJettonMultiple.c_str(), strApplyBankerPro.c_str(), strRobotJettonPro.c_str(),strArrDispatchCardPro.c_str());
	return true;
}

int     CGameNiuniuTable::GetProCardType()
{
	int iSumPro = 0;
	int iArrDispatchCardPro[CT_SPECIAL_MAX_TYPE] = { 0 };
	for (int i = 0; i < CT_SPECIAL_MAX_TYPE; i++)
	{
		iArrDispatchCardPro[i] = m_iArrDispatchCardPro[i];
		iSumPro += m_iArrDispatchCardPro[i];
	}
	
	int iProIndex = 1;
	int iRandNum = 0;
	if (iSumPro <= 0)
	{
		iProIndex = g_RandGen.RandRange(1, CT_SPECIAL_MAX_TYPE - 1);
	}
	else
	{
		iRandNum = g_RandGen.RandRange(0, iSumPro);
		for (; iProIndex < CT_SPECIAL_MAX_TYPE; iProIndex++)
		{
			if (iArrDispatchCardPro[iProIndex] == 0)
			{
				continue;
			}
			if (iRandNum <= iArrDispatchCardPro[iProIndex])
			{
				break;
			}
			else
			{
				iRandNum -= iArrDispatchCardPro[iProIndex];
			}
		}
	}

	string strAllPlayerUid;
	for (WORD i = 0; i<GAME_PLAYER; i++)
	{
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer != NULL)
		{
			strAllPlayerUid += CStringUtility::FormatToString("i:%d,uid:%d ", i, pPlayer->GetUID());
		}
	}

	LOG_DEBUG("roomid:%d,tableid:%d,iProIndex:%d,iSumPro:%d,iRandNum:%d,strAllPlayerUid:%s",
		GetRoomID(), GetTableID(), iProIndex, iSumPro, iRandNum, strAllPlayerUid.c_str());

	if (iProIndex >= CT_SPECIAL_MAX_TYPE)
	{
		iProIndex = CT_POINT;
	}

	return iProIndex;
}

bool	CGameNiuniuTable::ProbabilityDispatchPokerCard()
{
	bool bIsFlag = true;
	int iArProCardType[GAME_PLAYER] = { 0 };

	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		int iProIndex = GetProCardType();
		if (iProIndex > 0 && iProIndex < CT_SPECIAL_MAX_TYPE)
		{
			iArProCardType[i] = iProIndex;
		}
		else
		{
			iArProCardType[i] = CT_POINT;
		}
	}

	BYTE cbArTempCardData[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };

	bIsFlag = m_gameLogic.GetCardTypeData(iArProCardType, cbArTempCardData);
	if (bIsFlag)
	{
		memcpy(m_cbHandCardData, cbArTempCardData, sizeof(m_cbHandCardData));
	}

	string strAllPlayerUid;
	string strAllPlayerStatus;
	for (WORD i = 0; i<GAME_PLAYER; i++)
	{
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer != NULL)
		{
			strAllPlayerUid += CStringUtility::FormatToString("i:%d,uid:%d ", i, pPlayer->GetUID());
		}
		if (m_cbPlayStatus[i] == TRUE)
		{
			strAllPlayerStatus += CStringUtility::FormatToString("i:%d,t:%d,cd:0x%02X_0x%02X_0x%02X_0x%02X_0x%02X ",
				i, iArProCardType[i], m_cbHandCardData[i][0], m_cbHandCardData[i][1], m_cbHandCardData[i][2], m_cbHandCardData[i][3], m_cbHandCardData[i][4]);
		}
	}

	LOG_DEBUG("roomid:%d,tableid:%d,bIsFlag:%d,uid:%s,status:%s", GetRoomID(), GetTableID(), bIsFlag, strAllPlayerUid.c_str(), strAllPlayerStatus.c_str());

	return bIsFlag;
}

void    CGameNiuniuTable::ShutDown()
{

}

//复位桌子
void    CGameNiuniuTable::ResetTable()
{
    ResetGameData();

	//SetGameState(TABLE_STATE_APBNIU_FREE);
    ResetPlayerReady();
    SendSeatInfoToClient();
}

void    CGameNiuniuTable::OnTimeTick()
{
	OnTableTick();
    if(m_coolLogic.isTimeOut())
    {
        uint8 tableState = GetGameState();
        switch(tableState)
        {
        case TABLE_STATE_APBNIU_FREE:
            {
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d",GetRoomID(), GetTableID(), GetGameState());
				if (IsCanStartGame())
				{
					SetGameState(TABLE_STATE_APBNIU_READY_START);
					m_coolLogic.beginCooling(s_ReadyStartTime);

					SendReadyStartGame();
				}
            }break;
		case TABLE_STATE_APBNIU_READY_START:
			{
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());
				CheckPlayerScoreLessLeave();
				OnGameStart();
			}break;		
        case TABLE_STATE_APBNIU_GAME_START:
            {
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());
				SetGameState(TABLE_STATE_APBNIU_APPLY_BRANKER);
				m_coolLogic.beginCooling(s_ApplyBrankerTime);
				//m_coolRobot.beginCooling(g_RandGen.RandRange(500, 1000));
				OnRobotReadyApplyBanker();
            }break;
		case TABLE_STATE_APBNIU_APPLY_BRANKER:
			{
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());
				SetGameState(TABLE_STATE_APBNIU_MAKE_BRANKER);
				m_vecRobotApplyBanker.clear();
				OnTimeOutApplyBanker();
				InitBanker();
			}break;
		case TABLE_STATE_APBNIU_MAKE_BRANKER:
			{
				SetGameState(TABLE_STATE_APBNIU_PLACE_JETTON);
				m_coolLogic.beginCooling(s_AddScoreTime);				
				OnRobotReadyJetton();
			}break;
        case TABLE_STATE_APBNIU_PLACE_JETTON:
            {   
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());				
				m_vecRobotJetton.clear();				
				SetGameState(TABLE_STATE_APBNIU_SEND_CARD);
				m_coolLogic.beginCooling(s_ShowCardTime * (GetPlayGameCount() - 1) + 900);
				SendCardToClient();
            }break;		
		case TABLE_STATE_APBNIU_CHANGE_CARD:
			{
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());
				OnTimeOutChangeCard();
				SetGameState(TABLE_STATE_APBNIU_GAME_END);
				m_coolLogic.beginCooling(s_GameEndTime - ((GAME_PLAYER - GetPlayGameCount()) * 1000));
				OnGameEnd(INVALID_CHAIR, GER_NORMAL);
				//CheckNoOperPlayerLeave();
			}break;
		case TABLE_STATE_APBNIU_GAME_END:
			{
				//LOG_DEBUG("roomid:%d,tableid:%d,status:%d", GetRoomID(), GetTableID(), GetGameState());
				ResetTable();
				SetGameState(TABLE_STATE_APBNIU_FREE);
				CheckNoOperPlayerLeave();
				CheckPlayerScoreManyLeave();
				m_coolLogic.beginCooling(s_FreeTime);				
			}break;
        default:
            break;
        }
    }
	else if (GetGameState() == TABLE_STATE_APBNIU_CHANGE_CARD)
	{
		OnRobotChangeCard();
	}

	CheckAddRobot();
}

void CGameNiuniuTable::OnRobotReadyApplyBanker()
{
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		if (m_szApplyBanker[i] != FALSE)
		{
			continue;
		}
		CGamePlayer* pPlayer = GetPlayer(i);
		if (pPlayer == NULL || !pPlayer->IsRobot())
		{
			continue;
		}
		OnRobotRealApplyBankerScore(i);
	}
}

void CGameNiuniuTable::OnRobotReadyJetton()
{
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		CGamePlayer* pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot() == false)
		{
			continue;
		}
		if (m_lTableScore[i] == 0)
		{
			OnRobotRealJettonScore(i);			
		}
	}
}

void CGameNiuniuTable::OnRobotTick()
{
	//LOG_DEBUG("roomid:%d,tableid:%d,status:%d",//	GetRoomID(), GetTableID(), GetGameState());

	if (m_coolLogic.isTimeOut())
	{
		uint8 tableState = GetGameState();
		switch (tableState)
		{
			case TABLE_STATE_APBNIU_SEND_CARD:
			{
				SetGameState(TABLE_STATE_APBNIU_CHANGE_CARD);
				m_coolLogic.beginCooling(s_ChangeCardTime);			
			}break;
			default:
			{
				break;
			}
		}
	}
	if (GetGameState() == TABLE_STATE_APBNIU_APPLY_BRANKER)
	{
		OnRobotInApplyBanker();
	}
	else if (GetGameState() == TABLE_STATE_APBNIU_PLACE_JETTON)
	{
		OnRobotInJetton();
	}
}

// 游戏消息
int     CGameNiuniuTable::OnGameMessage(CGamePlayer* pPlayer,uint16 cmdID, const uint8* pkt_buf, uint16 buf_len)
{
    uint16 chairID = GetChairID(pPlayer);
    //LOG_DEBUG("收到玩家消息:%d--%d",chairID,cmdID);

	if (pPlayer == NULL)
	{
		LOG_DEBUG("table recv - roomid:%d,tableid:%d,chairID:%d,status:%d,cmdID:%d", GetRoomID(), GetTableID(), chairID, GetGameState(), cmdID);
		return 0;
	}
	LOG_DEBUG("table recv - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
		GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

	if (chairID < GAME_PLAYER)
	{
		m_szNoOperCount[chairID] = 0;
		m_szNoOperTrun[chairID] = 0;
	}

	if (m_cbPlayStatus[chairID] == FALSE)
	{
		LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
			GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

		net::msg_niuniu_game_status msg;
		msg.set_status(GetGameState());
		pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_GAME_STATUS_ERROR);

		return 0;
	}

    switch(cmdID)
    {
    case net::C2S_MSG_NIUNIU_APPLY_BANKER_REQ:// 申请庄家
        {
            if(GetGameState() != TABLE_STATE_APBNIU_APPLY_BRANKER)
			{
                //LOG_DEBUG("不是抢庄状态:%d",GetGameState());
				LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
					GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

				net::msg_niuniu_game_status msg;
				msg.set_status(GetGameState());
				pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_GAME_STATUS_ERROR);

                return 0;
            }
            net::msg_niuniu_apply_banker msg;
            PARSE_MSG_FROM_ARRAY(msg);

            return OnUserApplyBanker(chairID,msg.score());
        }break;
    case net::C2S_MSG_NIUNIU_PLACE_JETTON_REQ:// 加注
        {
            if(GetGameState() != TABLE_STATE_APBNIU_PLACE_JETTON)
			{
                //LOG_DEBUG("不是加注状态:%d",GetGameState());
				LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
					GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

				net::msg_niuniu_game_status msg;
				msg.set_status(GetGameState());
				pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_GAME_STATUS_ERROR);

                return 0;
            }
            net::msg_niuniu_place_jetton_req msg;
            PARSE_MSG_FROM_ARRAY(msg);

            return OnUserAddScore(chairID,msg.jetton_score());
        }break;
    case net::C2S_MSG_NIUNIU_CHANGE_CARD_REQ:// 摆牌
        {
            if(GetGameState() != TABLE_STATE_APBNIU_CHANGE_CARD)
			{
                //LOG_DEBUG("不是摆牌状态:%d",GetGameState());
				LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
					GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

				net::msg_niuniu_game_status msg;
				msg.set_status(GetGameState());
				pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_GAME_STATUS_ERROR);

                return 0;
            }
            net::msg_niuniu_change_card msg;
            PARSE_MSG_FROM_ARRAY(msg);
            if(msg.cards_size() < NIUNIU_CARD_NUM)
			{
                //LOG_ERROR("牌的数量不对:%d",msg.cards_size());
				LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,cmdID:%d",
					GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), cmdID);

				net::msg_niuniu_game_status msg;
				msg.set_status(GetGameState());
				pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_GAME_STATUS_ERROR);

                return 0;
            }
            BYTE cards[NIUNIU_CARD_NUM];
            ZeroMemory(cards,sizeof(cards));
            for(uint8 i=0;i<msg.cards_size() && i < NIUNIU_CARD_NUM;++i)
            {
                cards[i] = msg.cards(i);
            }
            return OnUserChangeCard(chairID,cards);
        }break;
	case net::C2S_MSG_NIUNIU_RECV_MASTER_CARD_REQ:// 
		{
			net::msg_niuniu_recv_master_card_req msg;
			PARSE_MSG_FROM_ARRAY(msg);

			vector<BYTE> vecChairID;
			vector<BYTE> vecCardData;
			for (uint8 i = 0; i < msg.chairid_size(); i++)
			{
				vecChairID.push_back(msg.chairid(i));
			}
			for (uint8 i = 0; i < msg.cards_size(); i++)
			{
				vecCardData.push_back(msg.cards(i));
			}
			return OnMasterUserOper(pPlayer, vecChairID, vecCardData);
		}break;
    default:
        return 0;
    }
    return 0;
}

void    CGameNiuniuTable::SendFourCardToClient()
{
	msg_niuniu_send_card_rep msg;
	msg.set_time_leave(m_coolLogic.getCoolTick());
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			msg.add_chairid(i);
		}
	}
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		string strPlayerCardData;
		msg.clear_cards();
		
		if (m_cbPlayStatus[i] == TRUE)
		{
			for (uint8 j = 0; j<NIUNIU_CARD_NUM; ++j)
			{
				if (j == (NIUNIU_CARD_NUM - 1))
				{
					msg.add_cards(0);
				}
				else
				{
					msg.add_cards(m_cbHandCardData[i][j]);

					strPlayerCardData += CStringUtility::FormatToString("0x%02X ", m_cbHandCardData[i][j]);

				}
			}
			
		}
		LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,strPlayerCardData:%s",
			GetRoomID(), GetTableID(), i, GetPlayerID(i), strPlayerCardData.c_str());

		//SendMsgToClient(i, &msg, net::S2C_MSG_NIUNIU_SEND_FOUR_CARD_REP);
	}
}

uint32 CGameNiuniuTable::GetPlayGameCount()
{
	uint32 num = 0;
	for (uint8 i = 0; i<m_vecPlayers.size(); ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			num++;
		}
	}
	return num;
}

// 游戏开始
bool    CGameNiuniuTable::OnGameStart()
{
	int32 uPlayerCount = 0;
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_vecPlayers[i].pPlayer != NULL)
		{
			uPlayerCount++;
		}
	}
	if (uPlayerCount <= 1)
	{
		// 倒计时的时候有人离开不能继续开始游戏
		
		SetGameState(TABLE_STATE_APBNIU_FREE);
		m_coolLogic.beginCooling(s_FreeTime);

		return false;
	}
    //LOG_DEBUG("game start");
	for(WORD i=0;i<GAME_PLAYER;i++)
	{
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			m_szApplyBanker[i] = 255;
			continue;
		}
		m_cbPlayStatus[i]   = TRUE;
	}

	m_lucky_flag = false;

	string strAllPlayerUid;
	string strAllPlayerStatus;
	for (WORD i = 0; i<GAME_PLAYER; i++)
	{
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer != NULL)
		{
			strAllPlayerUid += CStringUtility::FormatToString("i_%d-uid_%d-npc_%d ", i, pPlayer->GetUID(), m_szNoOperCount[i]);
		}
		if (m_cbPlayStatus[i] == TRUE)
		{
			strAllPlayerStatus += CStringUtility::FormatToString("i:%d,ps:%d",i,m_cbPlayStatus[i]);
		}
	}
	InitBlingLog(false);

	LOG_DEBUG("roomid:%d,tableid:%d,chessid:%s,uids:%s,status:%s", GetRoomID(), GetTableID(),GetChessID().c_str(), strAllPlayerUid.c_str(), strAllPlayerStatus.c_str());

	//构造数据
    net::msg_niuniu_start_rep msg;
	for (uint16 i = 0; i < JETTON_MULTIPLE_COUNT; ++i)
	{
		msg.add_jetton_multiple(m_lJettonMultiple[i]);
	}
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			msg.add_chairid(i);
		}
	}
    SendMsgToAll(&msg,net::S2C_MSG_NIUNIU_START);
    
    //服务费
    DeductStartFee(true);
	
	SetGameState(TABLE_STATE_APBNIU_GAME_START);
	m_coolLogic.beginCooling(s_GameStartTime);

	ProbabilityDispatchPokerCard();

	SetCardDataControl();

	LOG_DEBUG("XXXXX roomid:%d,tableid:%d", GetRoomID(), GetTableID());

	for (uint16 i = 0; i < GAME_PLAYER; ++i)
		if (m_cbPlayStatus[i] == TRUE)
			m_cbTableCardType[i] = m_gameLogic.GetCardType(m_cbHandCardData[i], NIUNIU_CARD_NUM);
	OnSendMasterCard();
    //SetRobotThinkTime();

	LOG_DEBUG("XXXXX roomid:%d,tableid:%d", GetRoomID(), GetTableID());

    return true;
}

bool    CGameNiuniuTable::OnSendMasterCard()
{
	bool bIsHaveMasterUser = false;
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			CGamePlayer * pPlayer = GetPlayer(i);
			if (pPlayer != NULL && pPlayer->GetCtrlFlag())
			{
				bIsHaveMasterUser = true;
			}
		}
	}
	if (bIsHaveMasterUser == false)
	{
		return false;
	}
	msg_niuniu_send_master_card_rep mmsg;
	mmsg.set_time_leave(m_coolLogic.getCoolTick());
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			mmsg.add_chairid(i);
			CGamePlayer * pPlayer = GetPlayer(i);
			if (pPlayer != NULL)
			{
				mmsg.add_isrobot(pPlayer->IsRobot());
			}
		}
		mmsg.add_card_types(m_cbTableCardType[i]);
	}
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			for (uint8 j = 0; j<NIUNIU_CARD_NUM; ++j)
			{
				mmsg.add_cards(m_cbHandCardData[i][j]);
			}
		}
	}

	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(m_cbHandCardData, vecRemainCardData);
	for (uint i = 0; i < vecRemainCardData.size(); i++)
	{
		mmsg.add_remain_cards(vecRemainCardData[i]);
	}

	map<uint32, tagUserControlCfg> mpCfgInfo;
	CDataCfgMgr::Instance().GetUserControlCfg(mpCfgInfo);

	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		CGamePlayer * pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			continue;
		}
		auto find_iter = mpCfgInfo.find(pPlayer->GetUID());
		if (find_iter != mpCfgInfo.end())
		{
			tagUserControlCfg tagCtrlCfg = find_iter->second;
			auto find_set = tagCtrlCfg.cgid.find(GetGameType());
			if (find_set != tagCtrlCfg.cgid.end())
			{
				SendMsgToClient(i, &mmsg, net::S2C_MSG_NIUNIU_SEND_MASTER_CARD_REP);

				LOG_DEBUG("master - roomid:%d,tableid:%d,i:%d,suid:%d,tuid:%d",
					GetRoomID(), GetTableID(), i, GetPlayerID(i), tagCtrlCfg.tuid);

			}
		}
	}

	return true;
}

//游戏结束
bool    CGameNiuniuTable::OnGameEnd(uint16 chairID,uint8 reason)
{
    //LOG_DEBUG("game end:%d--%d",chairID,reason);

	switch(reason)
	{
	case GER_NORMAL:
		{
		    int64 playerAllWinScore = CalculateScore(m_lWinScore, true); // modify by har
			WriteGameInfo();
            net::msg_niuniu_game_end msg;
			msg.set_time_leave(m_coolLogic.getCoolTick());
			msg.set_banker_id(m_wBankerUser);
			for (uint16 i = 0; i < GAME_PLAYER; ++i)
			{
				msg.add_card_types(m_cbTableCardType[i]);
				msg.add_win_multiple(m_winMultiple[i]);
				msg.add_player_score(m_lWinScore[i]);
				//msg.add_rob_multiple(m_lRobMultiple[i]);
				
				net::msg_cards* pcards = msg.add_table_cards();
				std::string strPlayerCard;
				for (uint8 j = 0; j < NIUNIU_CARD_NUM; ++j) {
					if (m_cbTableCardType[i] == CT_ERROR)
						pcards->add_cards(0); // 牌型为0这强制设置牌为0,避免中途进入的玩家座位也会发牌  add by har
					else
					    pcards->add_cards(m_cbHandCardData[i][j]);

					strPlayerCard = CStringUtility::FormatToString("card:0x%02X 0x%02X 0x%02X 0x%02X 0x%02X ",
						m_cbHandCardData[i][0], m_cbHandCardData[i][1], m_cbHandCardData[i][2], m_cbHandCardData[i][3], m_cbHandCardData[i][4]);

				}

				LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,szNoOperCount:%d,AccountsType:%d,CardType:%d,PlayStatus:%d,strPlayerCard:%s,winMultiple:%d,lWinScore:%lld",
					GetRoomID(), GetTableID(), i, GetPlayerID(i), m_szNoOperCount[i], m_cbBrankerSettleAccountsType, m_cbTableCardType[i], m_cbPlayStatus[i], strPlayerCard.c_str(), m_winMultiple[i], m_lWinScore[i]);
				
				if (m_cbPlayStatus[i] == TRUE)
				{
					if (m_cbTimeOutShowCard[i] == TRUE)
					{
						msg.add_change_chairid(GAME_PLAYER);
					}
					else
					{
						auto iter_find = std::find(std::begin(m_vecChangerChairID), std::end(m_vecChangerChairID), i);
						if (iter_find != std::end(m_vecChangerChairID))
						{
							int posIndex = std::distance(std::begin(m_vecChangerChairID), iter_find);
							msg.add_change_chairid(posIndex);
						}
						else
						{
							msg.add_change_chairid(GAME_PLAYER);
						}
					}
				}
				else
				{
					msg.add_change_chairid(GAME_PLAYER + 1);
				}
			}

			for (uint16 i = 0; i < GAME_PLAYER; ++i)
			{
				//LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,i:%d,status:%d,uid:%d,m_szNoOperCount:%d,",
				//	GetRoomID(), GetTableID(), m_wBankerUser, i, GetGameState(), GetPlayerID(i), m_szNoOperCount[i]);

				if (i== m_wBankerUser)
				{
					if (m_szNoOperCount[i] >= 2)
					{
						m_szNoOperTrun[i]++;
					}
				}
				else
				{
					if (m_szNoOperCount[i] >=3)
					{
						m_szNoOperTrun[i]++;
					}
				}
				m_szNoOperCount[i] = 0;
			}


			msg.set_settle_accounts_type(m_cbBrankerSettleAccountsType);

			//for (const auto valChairID : m_vecChangerChairID)
			//{
			//	msg.add_change_chairid(valChairID);
			//}

            SendMsgToAll(&msg,net::S2C_MSG_NIUNIU_GAME_END);
			
			//更新幸运值数据   
			if (m_lucky_flag)
			{
				for (uint16 i = 0; i < GAME_PLAYER; ++i)
				{
					CGamePlayer * pGamePlayer = GetPlayer(i);
					if (pGamePlayer != NULL)
					{
						auto iter = m_set_ctrl_lucky_uid.find(pGamePlayer->GetUID());
						if (iter != m_set_ctrl_lucky_uid.end())
						{
							pGamePlayer->SetLuckyInfo(GetRoomID(), m_lWinScore[i]);
							LOG_DEBUG("set current player lucky info. uid:%d roomid:%d score:%d", pGamePlayer->GetUID(), GetRoomID(), m_lWinScore[i]);
						}
					}
				}
			}

			//更新活跃福利数据
            int aw_ctrl_chairid = INVALID_CHAIR;
            for (uint16 i = 0; i < GAME_PLAYER; ++i)
            {
				if (GetPlayerID(i) == m_aw_ctrl_uid)
                {
                    aw_ctrl_chairid = i;
                    break;
                }               
            }
            if(aw_ctrl_chairid!= INVALID_CHAIR)
            {
                int64 curr_win = m_lWinScore[aw_ctrl_chairid];
                UpdateActiveWelfareInfo(m_aw_ctrl_uid, curr_win);
            }
            else
            {
                LOG_ERROR("Get aw ctrl player chairid is fail. m_aw_ctrl_uid:%d", m_aw_ctrl_uid);
            }           

            SaveBlingLog();
			LOG_DEBUG("OnGameEnd2 roomid:%d,tableid:%d,playerAllWinScore:%lld", GetRoomID(), GetTableID(), playerAllWinScore);
			m_pHostRoom->UpdateStock(this, playerAllWinScore); // add by har
			OnTableGameEnd();
		}break;
    default:
        break;
	}
	return false;
}

//用户同意
bool    CGameNiuniuTable::OnActionUserOnReady(WORD wChairID,CGamePlayer* pPlayer)
{
    if(GetReadyNum() == 2 && m_coolLogic.getCoolTick() < 1000){
        m_coolLogic.beginCooling(1500);// 准备后等一秒
    }
    return true;
}

//玩家进入或离开
void    CGameNiuniuTable::OnPlayerJoin(bool isJoin,uint16 chairID,CGamePlayer* pPlayer)
{
    CGameTable::OnPlayerJoin(isJoin,chairID,pPlayer);            
    if(isJoin)
	{
        SendGameScene(pPlayer);
    }
	else
	{

    }
    if(chairID < GAME_PLAYER)
	{
        m_szNoOperCount[chairID] = 0;
		m_szNoOperTrun[chairID] = 0;
    }
}

// 发送场景信息(断线重连)
void    CGameNiuniuTable::SendGameScene(CGamePlayer* pPlayer)
{
    uint16 chairID = GetChairID(pPlayer);
    //LOG_DEBUG("send game scene:%d", chairID);

	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d",
		GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID));

	switch(m_gameState)
	{
	case net::TABLE_STATE_APBNIU_FREE: //空闲状态
		{
            net::msg_niuniu_game_info_free_rep msg;
            msg.set_banker_id(m_wBankerUser);
			msg.set_time_leave(s_FreeTime);			

            pPlayer->SendMsgToClient(&msg,net::S2C_MSG_NIUNIU_GAME_FREE_INFO);

		}break;
	case net::TABLE_STATE_APBNIU_READY_START:
	case net::TABLE_STATE_APBNIU_GAME_START:
	case net::TABLE_STATE_APBNIU_APPLY_BRANKER:
	case net::TABLE_STATE_APBNIU_MAKE_BRANKER:		
    case net::TABLE_STATE_APBNIU_PLACE_JETTON:	
	case net::TABLE_STATE_APBNIU_SEND_CARD:
	case net::TABLE_STATE_APBNIU_CHANGE_CARD:
	case net::TABLE_STATE_APBNIU_GAME_END:
		{
            net::msg_niuniu_game_info_play_rep msg;
            msg.set_game_status(GetGameState());
            msg.set_banker_id(m_wBankerUser);
            msg.set_time_leave(m_coolLogic.getCoolTick());
			
            for(uint16 i=0;i<GAME_PLAYER;++i)
			{
				msg.add_card_types(m_cbTableCardType[i]);
                msg.add_all_jetton_score(m_lTableScore[i]);
                msg.add_show_cards(m_szShowCardState[i]);
                msg.add_player_status(m_cbPlayStatus[i]);
                msg.add_apply_list(m_szApplyBanker[i]);
				msg.add_apply_multiple(m_lRobMultiple[i]);
                net::msg_cards* pcards = msg.add_table_cards();
				if (m_gameState >= TABLE_STATE_APBNIU_SEND_CARD)
				{
					for (uint8 j = 0; j<NIUNIU_CARD_NUM; ++j)
					{
						pcards->add_cards(m_cbHandCardData[i][j]);
					}
				}
				else
				{
					if (chairID == i)
					{
						for (uint8 j = 0; j<NIUNIU_CARD_NUM; ++j)
						{
							if (j == (NIUNIU_CARD_NUM - 1))
							{
								pcards->add_cards(0);
							}
							else
							{
								pcards->add_cards(m_cbHandCardData[i][j]);
							}
						}
					}
					else
					{
						for (uint8 j = 0; j<NIUNIU_CARD_NUM; ++j)
						{
							pcards->add_cards(0);
						}
					}

				}
                //msg.add_turn_max_score(m_lTurnMaxScore[i]);
            }
			for (uint16 i = 0; i < JETTON_MULTIPLE_COUNT; ++i)
			{
				msg.add_jetton_multiple(m_lJettonMultiple[i]);
			}

            pPlayer->SendMsgToClient(&msg,net::S2C_MSG_NIUNIU_GAME_PLAY_INFO);
			OnSendMasterCard();
		}break;
	}
}

int64    CGameNiuniuTable::CalcPlayerInfo(uint16 chairID,int64 winScore)
{
	if (winScore == 0 || chairID >= GAME_PLAYER)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,uid:%d,chairid:%d,socre:%lld",GetRoomID(),GetTableID(),GetPlayerID(chairID), chairID, winScore);
		return 0;
	}
    //LOG_DEBUG("report game to lobby:%d  %lld",chairID,winScore);
    uint32 uid = m_vecPlayers[chairID].uid;

    int64 fee = CalcPlayerGameInfo(uid,winScore);

	return fee;
}

// 重置游戏数据
void    CGameNiuniuTable::ResetGameData()
{

	ZeroMemory(m_cbPlayStatus, sizeof(m_cbPlayStatus));
	ZeroMemory(m_szApplyBanker, sizeof(m_szApplyBanker));
	ZeroMemory(m_szShowCardState, sizeof(m_szShowCardState));
	ZeroMemory(m_cbTimeOutShowCard, sizeof(m_cbTimeOutShowCard));
	ZeroMemory(m_cbHandCardData, sizeof(m_cbHandCardData));
	ZeroMemory(m_lRobMultiple, sizeof(m_lRobMultiple));
	ZeroMemory(m_lTableScore, sizeof(m_lTableScore));
	ZeroMemory(m_cbChangeCardData, sizeof(m_cbChangeCardData));

	ZeroMemory(m_cbTableCardType, sizeof(m_cbTableCardType));         //桌面牌型
	ZeroMemory(m_winMultiple, sizeof(m_winMultiple));                 //输赢倍数
	ZeroMemory(m_lWinScore, sizeof(m_lWinScore));                     //输赢分数
	ZeroMemory(m_lTurnMaxScore, sizeof(m_lTurnMaxScore));             //最大下注

	m_wBankerUser = INVALID_CHAIR;

	m_bIsNoviceWelfareCtrl = false;
	m_bIsProgressControlPalyer = false;
	m_bIsMasterUserOper = false;
	m_vecChangerChairID.clear();
	m_vecRobotApplyBanker.clear();
	m_vecRobotJetton.clear();
}

// 获取单个下注的是机器人还是玩家  add by har
void CGameNiuniuTable::IsRobotOrPlayerJetton(CGamePlayer *pPlayer, bool &isAllPlayer, bool &isAllRobot) {
	DealIsRobotOrPlayerJetton(pPlayer, isAllPlayer, isAllRobot, m_cbPlayStatus);
}

// 写入出牌log
void    CGameNiuniuTable::WriteOutCardLog(uint16 chairID,uint8 cardData[],uint8 cardCount,int64 score)
{
    uint8 cardType = m_gameLogic.GetCardType(cardData,cardCount);
    Json::Value logValue;
    logValue["p"] = chairID;
    logValue["s"] = score;
    logValue["cardtype"] = cardType;
    for(uint32 i=0;i<cardCount;++i){
        logValue["c"].append(cardData[i]);
    }
    m_operLog["card"].append(logValue);
}

// 写入庄家位log
void    CGameNiuniuTable::WriteBankerLog(uint16 chairID)
{
    m_operLog["banker"] = chairID;
	m_operLog["bscore"] = GetBaseScore();	
}

void    CGameNiuniuTable::WriteGameInfo()
{
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}

		// 写入手牌log
		WriteOutCardLog(i, m_cbHandCardData[i], NIUNIU_CARD_NUM, m_lTableScore[i]);

		Json::Value logValue;
		logValue["ci"] = i;
		logValue["rm"] = m_lRobMultiple[i];
		logValue["wm"] = m_winMultiple[i];
		logValue["ws"] = m_lWinScore[i];
		m_operLog["info"].append(logValue);
	}
	m_operLog["muo"] = m_bIsMasterUserOper;
}

// 是否能够开始游戏
bool    CGameNiuniuTable::IsCanStartGame()
{
	uint32 uPlayerCount = 0;
    for(uint16 i=0;i<GAME_PLAYER;++i)
    {
        if(m_vecPlayers[i].pPlayer != NULL)
		{
			uPlayerCount++;
        }
    }
	if (uPlayerCount >= 2)
	{
		return true;
	}
    return false;
}

void CGameNiuniuTable::SendReadyStartGame()
{
	net::msg_niuniu_ready_start_rep msg;
	msg.set_time_leave(m_coolLogic.getCoolTick());
	SendMsgToAll(&msg, net::S2C_MSG_NIUNIU_READY_START);
}

// 检测筹码是否正确
bool    CGameNiuniuTable::CheckJetton(uint16 chairID,int64 score)
{
    if(score < 0 || score > m_lTurnMaxScore[chairID])
	{
        //LOG_ERROR("下注错误%lld,%lld",score,m_lTurnMaxScore[chairID]);
        return false;
    }
    return true;
}

// 获得机器人的铁壁
int64   CGameNiuniuTable::GetRobotJetton(uint16 chairID)
{
    if(g_RandGen.RandRatio(20,100))
        return m_lTurnMaxScore[chairID]/8;
    if(g_RandGen.RandRatio(30,100))
        return m_lTurnMaxScore[chairID]/4;
    if(g_RandGen.RandRatio(30,100))
        return m_lTurnMaxScore[chairID]/2;
    return m_lTurnMaxScore[chairID];
}

uint16  CGameNiuniuTable::GetPlayNum()
{
    //人数统计
    WORD wPlayerCount=0;
    for(WORD i=0;i<GAME_PLAYER;i++)
    {
        if(m_cbPlayStatus[i]==TRUE)
            wPlayerCount++;
    }
    return wPlayerCount;
}

void    CGameNiuniuTable::InitBanker()
{
    net::msg_niuniu_banker_result_rep msg;
    vector<uint16> vecApplyMultiple[NIU_MULTIPLE_COUNT];
	bool bIsHaveApplyBranker = false;
	string strAllPlayerUid;
	int iApplyCount = 0;
    for(uint16 i=0;i<GAME_PLAYER;++i)
	{
		strAllPlayerUid += CStringUtility::FormatToString("i:%d,uid:%d ", i, GetPlayerID(i));
		msg.add_apply_list(m_szApplyBanker[i]);
		msg.add_apply_multiple(m_lRobMultiple[i]);
		if (m_lRobMultiple[i] >0 && m_lRobMultiple[i] < NIU_MULTIPLE_COUNT)
		{
			iApplyCount++;
			vecApplyMultiple[m_lRobMultiple[i]].push_back(i);
			bIsHaveApplyBranker = true;
		}
    }
    if(bIsHaveApplyBranker)
	{
		for (int i = NIU_MULTIPLE_COUNT - 1; i >= 0; i--)
		{
			if (vecApplyMultiple[i].size() > 0)
			{
				int iRandBrankerIndex = g_RandGen.RandRange(0, vecApplyMultiple[i].size() - 1);
				m_wBankerUser = vecApplyMultiple[i][iRandBrankerIndex];
				break;
			}
			if (i == 0)
			{
				break;
			}
		}
    }
	else 
	{
        m_wBankerUser = g_RandGen.RandUInt() % GAME_PLAYER;
        while(m_cbPlayStatus[m_wBankerUser] == FALSE)
		{
            m_wBankerUser = (m_wBankerUser + 1) % GAME_PLAYER;
        }
    }

	if (m_wBankerUser == INVALID_CHAIR)
	{
		LOG_DEBUG("error_invalid - roomid:%d,tableid:%d,bIsHaveApplyBranker:%d,m_wBankerUser:%d,strAllPlayerUid:%s",
			GetRoomID(),GetTableID(), bIsHaveApplyBranker, m_wBankerUser, strAllPlayerUid.c_str());
		m_wBankerUser = g_RandGen.RandUInt() % GAME_PLAYER;
		while (m_cbPlayStatus[m_wBankerUser] == FALSE)
		{
			m_wBankerUser = (m_wBankerUser + 1) % GAME_PLAYER;
		}
	}

	LOG_DEBUG("end - roomid:%d,tableid:%d,bIsHaveApplyBranker:%d,m_wBankerUser:%d,strAllPlayerUid:%s",
		GetRoomID(), GetTableID(), bIsHaveApplyBranker, m_wBankerUser, strAllPlayerUid.c_str());


    //庄家积分
    int64 lBankerScore = GetPlayerCurScore(GetPlayer(m_wBankerUser));
    //玩家人数
    WORD wUserCount=0;
    for (WORD i=0;i<GAME_PLAYER;i++)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			wUserCount++;
		}
    }
	if (iApplyCount == 0)
	{
		iApplyCount = wUserCount;
	}
    //最大下注
    for(WORD i=0;i<GAME_PLAYER;i++)
    {
		if (m_cbPlayStatus[i] != TRUE || i == m_wBankerUser)
		{
			continue;
		}
        //获取积分
        int64 lScore = GetPlayerCurScore(GetPlayer(i));
        //下注变量
		m_lTurnMaxScore[i] = lScore;// min(lBankerScore / (wUserCount - 1) / 3, lScore / 3);
    }

 //   for(uint16 i=0;i<GAME_PLAYER;++i)
 //   {
 //       msg.add_turn_max_score(m_lTurnMaxScore[i]);
 //   }
	std::string strMakeBrankerTime;
	//strMakeBrankerTime += CStringUtility::FormatToString(" diffChairId:");

	int iArrDiffTime[GAME_PLAYER] = { 0 };
	int iArrDiffCount[GAME_PLAYER] = { 0 };
	for (int i = 0; i < GAME_PLAYER; i++)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			int diffChairId = 0;
			if (m_wBankerUser > i)
			{
				diffChairId = m_wBankerUser - i;
			}
			else
			{
				diffChairId = i - m_wBankerUser;
			}
			int iDiffPlayerCount = (diffChairId + GAME_PLAYER);
			iArrDiffTime[i] = (iDiffPlayerCount % GAME_PLAYER);
			
			//strMakeBrankerTime += CStringUtility::FormatToString(" i_%d,b_%d,d_%d,p_%d,t_%d ", i, m_wBankerUser, diffChairId, iDiffPlayerCount, iArrDiffTime[i]);

			iArrDiffCount[i] = ((3-1) * iApplyCount + iArrDiffTime[i]);
		}
	}
	int iArrTrunTime[GAME_PLAYER] = { 0 };
	for (int i = 0; i < GAME_PLAYER; i++)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			for (int j = 1; j <= iArrDiffCount[i]; j++)
			{
				iArrTrunTime[i] += (10 + (j - 1) * 25);
			}
		}
	}
	int uMakeBranker = 0;
	int iArrEndTime[GAME_PLAYER] = { 0 };
	for (int i = 0; i < GAME_PLAYER; i++)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			iArrEndTime[i] += (iArrTrunTime[i] + 2000);
			if (uMakeBranker < iArrEndTime[i])
			{
				uMakeBranker = iArrEndTime[i];
			}
		}
		msg.add_time_leave(iArrEndTime[i]);
	}
		
	strMakeBrankerTime += CStringUtility::FormatToString(" 4_iArrEndTime:");
	for (int i = 0; i < GAME_PLAYER; i++)
	{
		strMakeBrankerTime += CStringUtility::FormatToString("%d ", iArrEndTime[i]);
	}

	LOG_DEBUG("branker - roomid:%d,tableid:%d,chessid:%s,Banker:%d,iApplyCount:%d,uid:%d,strMakeBrankerTime:%s",
		GetRoomID(), GetTableID(),GetChessID().c_str(), m_wBankerUser, iApplyCount, GetPlayerID(m_wBankerUser), strMakeBrankerTime.c_str());

	m_coolLogic.beginCooling(uMakeBranker);
    msg.set_banker_id(m_wBankerUser);
	//msg.set_time_leave(m_coolLogic.getCoolTick());
	//msg.set_time_leave(uMakeBranker);
    SendMsgToAll(&msg,net::S2C_MSG_NIUNIU_BANKER_RESULT);

    WriteBankerLog(m_wBankerUser);
}

void    CGameNiuniuTable::SendCardToClient()
{
	LOG_DEBUG("SendCardToClient - roomid:%d,tableid:%d", GetRoomID(), GetTableID());
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (i != m_wBankerUser && m_cbPlayStatus[i] == TRUE && m_lTableScore[i] == 0)
		{
			CGamePlayer* pPlayer = GetPlayer(i);
			bool bIsRobot = false;
			if (pPlayer != NULL)
			{
				bIsRobot = pPlayer->IsRobot();
			}
			if (bIsRobot)
			{
				OnRobotJettonScore(i);
			}
			else
			{
				OnUserAddScore(i, m_lJettonMultiple[0]);
			}
			m_szNoOperCount[i]++;			

			LOG_DEBUG("time_out - roomid:%d,tableid:%d,i:%d,status:%d,uid:%d,isrobot:%d,m_szNoOperCount:%d,",
				GetRoomID(), GetTableID(), i, GetGameState(), GetPlayerID(i), bIsRobot, m_szNoOperCount[i]);			
		}
	}

	// add by har
	if (m_isNeedCheckStock) 
	{
		m_isNeedCheckStock = false;
		bool isHasCtrl = false; // 是否存在点控
		map<uint32, tagUserControlCfg> mpCfgInfo;
		CDataCfgMgr::Instance().GetUserControlCfg(mpCfgInfo);
		for (uint16 i = 0; i < GAME_PLAYER; ++i) {
			CGamePlayer *pPlayer = GetPlayer(i);
			if (pPlayer == NULL)
				continue;
			map<uint32, tagUserControlCfg>::iterator find_iter = mpCfgInfo.find(pPlayer->GetUID());
			if (find_iter != mpCfgInfo.end()) {
				tagUserControlCfg tagCtrlCfg = find_iter->second;
				set<uint8>::iterator find_set = tagCtrlCfg.cgid.find(GetGameType());
				if (find_set != tagCtrlCfg.cgid.end()) {
					isHasCtrl = true;
					break;
				}	
			}
		}
		bool bIsStockControl = false;
		if (!isHasCtrl)
		    bIsStockControl = SetStockWinLose();
		LOG_DEBUG("CGameNiuniuTable::SendCardToClient roomid:%d,tableid:%d,isHasCtrl:%d,bIsStockControl:%d",
			GetRoomID(), GetTableID(), isHasCtrl, bIsStockControl);
	} // add by har end

	msg_niuniu_send_card_rep msg;
	msg.set_time_leave(m_coolLogic.getCoolTick());
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			msg.add_chairid(i);
			m_cbTableCardType[i] = m_gameLogic.GetCardType(m_cbHandCardData[i], NIUNIU_CARD_NUM);
		}
		msg.add_card_types(m_cbTableCardType[i]);
	}
    for(uint16 i=0;i<GAME_PLAYER;++i)
	{
		msg.clear_cards();
        if(m_cbPlayStatus[i] == TRUE)
		{
            for(uint8 j=0;j<NIUNIU_CARD_NUM;++j)
			{
                msg.add_cards(m_cbHandCardData[i][j]);
            }
        }
        SendMsgToClient(i,&msg,net::S2C_MSG_NIUNIU_SEND_CARD_REP);
    }
	SetIsAllRobotOrPlayerJetton(IsAllRobotOrPlayerJetton()); // add by har
}

int64 CGameNiuniuTable::GetPlayerAddScore(uint32 chairID)
{
	int64 score = 5;   //默认值最低5倍
	if (chairID >= GAME_PLAYER)
	{
		return score;
	}
	if(m_lTableScore[chairID] > 0)
	{
		score = m_lTableScore[chairID];
	}
	return score;
}

int64 CGameNiuniuTable::GetPlayerRobMultiple(uint32 chairID)
{
	int64 multiple = 1;
	if (m_wBankerUser >= GAME_PLAYER)
	{
		return multiple;
	}
	if (m_lRobMultiple[m_wBankerUser] > 0)
	{
		multiple = m_lRobMultiple[m_wBankerUser];
	}
	return multiple;
}

// 结算分数
int64   CGameNiuniuTable::CalculateScore(int64 szWinScore[GAME_PLAYER], bool isComputeFee)
{
	int64 playerAllWinScore = 0;
	bool bBrankerTakeAll = true;
	bool bBrankerCompensation = true;

	int64 lWinScore[GAME_PLAYER] = { 0 }; // 正常输赢

	//输赢计算
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		int iWinFlag = m_gameLogic.CompareCard(m_cbHandCardData[m_wBankerUser], NIUNIU_CARD_NUM, m_cbHandCardData[i], NIUNIU_CARD_NUM, m_winMultiple[i]);
		m_cbTableCardType[i] = m_gameLogic.GetCardType(m_cbHandCardData[i], NIUNIU_CARD_NUM);
		int64 lLostWinFlag = 0;
		if (iWinFlag == 1)
		{
			lLostWinFlag = 1;
			bBrankerTakeAll = false;
		}
		else
		{
			lLostWinFlag = -1;
			bBrankerCompensation = false;
		}
		int64 lLostWinScore = lLostWinFlag * GetBaseScore() * GetPlayerAddScore(i) * GetPlayerRobMultiple(i) * m_winMultiple[i];
		lWinScore[i] += lLostWinScore;
		lWinScore[m_wBankerUser] -= lLostWinScore;
		//LOG_DEBUG("stand - roomid:%d,tableid:%d,i:%d,uid:%d,lWinScore:%lld,lLostWinScore:%lld,lLostWinFlag:%lld,BaseScore:%lld,lTableScore:%lld,RobMultiple:%lld,winMultiple:%d,CardType:%d",
		//	GetRoomID(),GetTableID(),i,GetPlayerID(i), lWinScore[i],lLostWinScore,lLostWinFlag, GetBaseScore(), m_lTableScore[i], m_lRobMultiple[i], m_winMultiple[i], m_cbTableCardType[i]);
	}
	m_cbTableCardType[m_wBankerUser] = m_gameLogic.GetCardType(m_cbHandCardData[m_wBankerUser], NIUNIU_CARD_NUM);

	//闲家输赢限制
	int64 lReadyWinScore[GAME_PLAYER] = { 0 }; // 准备输赢
	//memcpy(lReadyWinScore, lWinScore, sizeof(lReadyWinScore));
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		int64 lPlayerCurScore = GetPlayerCurScore(GetPlayer(i));
		if (lWinScore[i] >= 0)
		{// win
			if (lWinScore[i] <= lPlayerCurScore)
			{
				lReadyWinScore[i] = lWinScore[i];
			}
			else
			{
				lReadyWinScore[i] = lPlayerCurScore;
			}
		}
		else
		{// lost
			if ((-lWinScore[i]) <= lPlayerCurScore)
			{// 够输
				lReadyWinScore[i] = lWinScore[i];
			}
			else
			{// 不够输
				lReadyWinScore[i] = (-lPlayerCurScore);
			}
		}

		lReadyWinScore[m_wBankerUser] -= lReadyWinScore[i];

		//LOG_DEBUG("ready - roomid:%d,tableid:%d,buser:%d,i:%d,uid:%d,lReadyWinScore:%lld,lPlayerCurScore:%lld,breankerWinScore:%lld",
		//	GetRoomID(), GetTableID(), m_wBankerUser, i, GetPlayerID(i), lReadyWinScore[i], lPlayerCurScore, lReadyWinScore[m_wBankerUser]);
	}

	int64 lBrankerCurScore = GetPlayerCurScore(GetPlayer(m_wBankerUser));
	int64 lBrankerReadyLostScore = 0;
	int64 lBrankerReadyWinScore = 0;

	int64 lCaclRealWinScore[GAME_PLAYER] = { 0 };

	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		if (lReadyWinScore[i] > 0)
		{
			lBrankerReadyLostScore += lReadyWinScore[i];
		}
		else
		{
			lBrankerReadyWinScore -= lReadyWinScore[i];

			lCaclRealWinScore[i] = lReadyWinScore[i];
		}
	}
	
	int64 lRealWinScore[GAME_PLAYER] = { 0 };
	bool bIsBrankerScoreFix = false;
	bool bOperedChairID[GAME_PLAYER] = { 0 };	

	if (lBrankerReadyWinScore > lBrankerCurScore)
	{// 庄家赢太多 输的闲家按照倍数输给庄家筹码 lBrankerReadyWinScore
		bIsBrankerScoreFix = true;

		double fBrankerCurScore = (double)lBrankerCurScore;
		double fAllPlayerOnlyLostScore = (double)lBrankerReadyWinScore;

		double fReadyWinScore[GAME_PLAYER] = { 0.0 };
		for (uint16 i = 0; i < GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
			{
				continue;
			}
			if (lReadyWinScore[i] < 0)
			{
				int64 lTempReadyWinScore = (-lReadyWinScore[i]);
				fReadyWinScore[i] = (double)lTempReadyWinScore;
			}
		}
		if (fAllPlayerOnlyLostScore > 0.0)
		{
			for (uint16 i = 0; i < GAME_PLAYER; ++i)
			{
				if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
				{
					continue;
				}
				if (fReadyWinScore[i] > 0.0)
				{
					double fRealWinScore = fBrankerCurScore * fReadyWinScore[i] / fAllPlayerOnlyLostScore;
					int64 lTempReadyWinScore = (int64)fRealWinScore;
					lRealWinScore[i] = (-lTempReadyWinScore);

					lCaclRealWinScore[i] = lRealWinScore[i];

					bOperedChairID[i] = true;
				}
			}
		}
	}

	int64 lBrankerRealWin = 0;
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		if (lCaclRealWinScore[i] < 0)
		{
			lBrankerRealWin -= lCaclRealWinScore[i];
		}
	}
	lBrankerCurScore += lBrankerRealWin;

	if (lBrankerReadyLostScore > lBrankerCurScore)
	{// 庄家输太多 赢的闲家按照倍数分庄家筹码 lBrankerCurScore
		bIsBrankerScoreFix = true;

		double fBrankerCurScore = (double)lBrankerCurScore;
		double fAllPlayerOnlyWinScore = (double)lBrankerReadyLostScore;

		double fReadyWinScore[GAME_PLAYER] = { 0.0 };
		for (uint16 i = 0; i < GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
			{
				continue;
			}
			if (lReadyWinScore[i]>0)
			{
				fReadyWinScore[i] = (double)lReadyWinScore[i];
			}
		}
		if (fAllPlayerOnlyWinScore > 0.0)
		{
			for (uint16 i = 0; i < GAME_PLAYER; ++i)
			{
				if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
				{
					continue;
				}
				if (fReadyWinScore[i] > 0.0)
				{
					double fRealWinScore = fBrankerCurScore * fReadyWinScore[i] / fAllPlayerOnlyWinScore;
					lRealWinScore[i] = (int64)fRealWinScore;
					bOperedChairID[i] = true;
				}
			}
		}
	}

	if(bIsBrankerScoreFix == false)
	{
		for (uint16 i = 0; i < GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
			{
				continue;
			}
			lRealWinScore[i] = lReadyWinScore[i];
		}
	}
	else
	{
		for (uint16 i = 0; i < GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
			{
				continue;
			}
			if (bOperedChairID[i] == false)
			{
				lRealWinScore[i] = lReadyWinScore[i];
			}
		}
	}
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		lRealWinScore[m_wBankerUser] += lRealWinScore[i];
	}

	//LOG_DEBUG("brank - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,lBrankerCurScore:%lld,lReadyWinScore:%lld,lBrankerReadlost:%lld win:%lld,bIsBrankerScoreFix:%d,CardType:%d",
	//	GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), lBrankerCurScore, lReadyWinScore[m_wBankerUser], lBrankerReadyLostScore, lBrankerReadyWinScore, bIsBrankerScoreFix, m_cbTableCardType[m_wBankerUser]);

	//输赢赋值
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		szWinScore[i] += lRealWinScore[i];
		szWinScore[m_wBankerUser] -= lRealWinScore[i];
		if (!isComputeFee) {
			CGamePlayer *pPlayer = GetPlayer(i);
			if (pPlayer != NULL && !pPlayer->IsRobot())
				playerAllWinScore += szWinScore[i]; // add by har
		}
	}

	if (!isComputeFee) {
		CGamePlayer *pBankPlayer = GetPlayer(m_wBankerUser);
		if (pBankPlayer != NULL && !pBankPlayer->IsRobot())
			playerAllWinScore += szWinScore[m_wBankerUser]; // add by har
		return playerAllWinScore;
	}

	string strAllPlayerUidScore;
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer != NULL)
		{
			strAllPlayerUidScore += CStringUtility::FormatToString("i:%d,uid:%d,isRobot:%d,score:%lld,winMultiple:%d,lWinScore:%lld,lReadyWinScore:%lld,cbTableCardType:%d,lCaclRealWinScore:%d,lRealWinScore:%lld,bOperedChairID[i]:%d ", 
				i, pPlayer->GetUID(), pPlayer->IsRobot(), szWinScore[i], m_winMultiple[i], lWinScore[i], lReadyWinScore[i], m_cbTableCardType[i], lCaclRealWinScore[i], lRealWinScore[i], bOperedChairID[i]);
		}
	}

    for(uint16 i=0;i<GAME_PLAYER;++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
        int64 fee = CalcPlayerInfo(i, szWinScore[i]);
		szWinScore[i] += fee;
		CGamePlayer *pPlayer = GetPlayer(i);
		if (pPlayer != NULL && !pPlayer->IsRobot())
			playerAllWinScore += szWinScore[i]; // add by har
        //CCommonLogic::LogCardString(m_cbHandCardData[i], NIUNIU_CARD_NUM);
        //LOG_DEBUG("牌型:%d",m_cbTableCardType[i]);
    }
	if (bBrankerTakeAll)
	{
		m_cbBrankerSettleAccountsType = BRANKER_TYPE_TAKE_ALL;
	}
	else if (bBrankerCompensation)
	{
		m_cbBrankerSettleAccountsType = BRANKER_TYPE_COMPENSATION;
	}
	else
	{
		m_cbBrankerSettleAccountsType = BRANKER_TYPE_NULL;
	}
	LOG_DEBUG("isComputeFee roomid:%d,tableid:%d,playerAllWinScore:%lld,m_wBankerUser:%d,strAllPlayerUidScore:%s",
		GetRoomID(), GetTableID(), playerAllWinScore, m_wBankerUser, strAllPlayerUidScore.c_str());
	return playerAllWinScore;
}

// 检测提前结束
void    CGameNiuniuTable::CheckOverTime()
{
    uint8 gameState = GetGameState();
    bool bFlag = false;
    switch(gameState)
    {
    case TABLE_STATE_APBNIU_APPLY_BRANKER:
        {
            for(uint16 i=0;i<GAME_PLAYER;++i)
			{
                if(m_cbPlayStatus[i] == TRUE && m_szApplyBanker[i] == FALSE)
				{
                    goto EXIT_CHECK_TIME;
                }
            }
            bFlag = true;
        }break;
    case TABLE_STATE_APBNIU_PLACE_JETTON:
        {
            for(uint16 i=0;i<GAME_PLAYER;++i)
			{
				if (i == m_wBankerUser)
				{
					continue;
				}
                if(m_cbPlayStatus[i] == TRUE && m_lTableScore[i] == 0)
				{
                    goto EXIT_CHECK_TIME;
                }
            }
            bFlag = true;
        }break;
   // case TABLE_STATE_APBNIU_PLACE_JETTON:
   //     {
   //         for(uint16 i=0;i<GAME_PLAYER;++i)
			//{
   //             if(m_cbPlayStatus[i] == TRUE && m_szShowCardState[i] == 0)
			//	{
   //                 goto EXIT_CHECK_TIME;
   //             }
   //         }
   //         bFlag = true;
   //     }break;
    default:
        break;
    }
EXIT_CHECK_TIME:
    if(bFlag)
	{
        //LOG_DEBUG("清楚cd：%d",GetGameState());
		LOG_DEBUG("clear_cool - roomid:%d,tableid:%d,status:%d",GetRoomID(), GetTableID(), GetGameState());

        m_coolLogic.clearCool();
    }
}

// 检查挂机玩家踢出
void   CGameNiuniuTable::CheckNoOperPlayerLeave()
{
	//LOG_DEBUG("roomid:%d,tableid:%d,m_pHostRoom:%p", GetRoomID(), GetTableID(), m_pHostRoom);

	if (m_pHostRoom == NULL)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_pHostRoom:%p", GetRoomID(), GetTableID(), m_pHostRoom);
		return;
	}
    for(uint16 i=0;i<GAME_PLAYER;++i)
    {
		CGamePlayer* pPlayer = GetPlayer(i);
        if(pPlayer != NULL)
        {
			if (m_szNoOperTrun[i] >= 2)
			{
				uint32 uid = pPlayer->GetUID();
				bool bCanLeaveTable = CanLeaveTable(pPlayer);
				bool bLeaveTable = false;
				bool bCanLeaveRoom = false;
				bool bLeaveRoom = false;

				if (bCanLeaveTable)
				{
					bLeaveTable = LeaveTable(pPlayer);
					if (bLeaveTable)
					{
						bCanLeaveRoom = m_pHostRoom->CanLeaveRoom(pPlayer);
						if (bCanLeaveRoom)
						{
							bLeaveRoom = m_pHostRoom->LeaveRoom(pPlayer);
						}
						net::msg_leave_table_rep rep;
						rep.set_result(1);
						pPlayer->SendMsgToClient(&rep, net::S2C_MSG_LEAVE_TABLE_REP);
						m_szNoOperCount[i] = 0;
						m_szNoOperTrun[i] = 0;
					}
				}
				LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,szNoOperCount:%d,bCanLeaveTable:%d,bLeaveTable:%d,bCanLeaveRoom:%d,bLeaveRoom:%d",
					GetRoomID(), GetTableID(), i, uid, m_szNoOperCount[i], bCanLeaveTable, bLeaveTable, bCanLeaveRoom, bLeaveRoom);
			}
        }
    }
}

void   CGameNiuniuTable::CheckPlayerScoreLessNotify()
{
	if (m_pHostRoom == NULL)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_pHostRoom:%p",GetRoomID(), GetTableID(), m_pHostRoom);
		return;
	}
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
		if (pPlayer != NULL)
		{
			int64 lCurScore = GetPlayerCurScore(pPlayer);
			int64 lMinScore = m_pHostRoom->GetEnterMin();
			if (lCurScore < lMinScore)
			{
				uint32 uid = pPlayer->GetUID();
				net::msg_niuniu_socre_less rep;
				rep.set_result(1);
				pPlayer->SendMsgToClient(&rep, net::S2C_MSG_NIUNIU_SCORE_LESS);
				LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,lCurScore:%lld,lMinScore:%lld",
					GetRoomID(), GetTableID(), i, uid, lCurScore, lMinScore);
			}
		}
	}
}

void   CGameNiuniuTable::CheckPlayerScoreLessLeave()
{
	if (m_pHostRoom == NULL)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_pHostRoom:%p", GetRoomID(), GetTableID(), m_pHostRoom);
		return;
	}
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
		if (pPlayer != NULL && !pPlayer->IsRobot())
		{
			int64 lCurScore = GetPlayerCurScore(pPlayer);
			int64 lMinScore = m_pHostRoom->GetEnterMin();
			if (lCurScore < lMinScore)
			{
				uint32 uid = pPlayer->GetUID();
				bool bCanLeaveTable = CanLeaveTable(pPlayer);
				bool bLeaveTable = false;
				bool bCanLeaveRoom = false;
				bool bLeaveRoom = false;
				if (bCanLeaveTable)
				{
					bLeaveTable = LeaveTable(pPlayer);
					if (bLeaveTable)
					{
						bCanLeaveRoom = m_pHostRoom->CanLeaveRoom(pPlayer);
						if (bCanLeaveRoom)
						{
							bLeaveRoom = m_pHostRoom->LeaveRoom(pPlayer);
						}
						net::msg_leave_table_rep rep;
						rep.set_result(1);
						pPlayer->SendMsgToClient(&rep, net::S2C_MSG_LEAVE_TABLE_REP);
						m_szNoOperCount[i] = 0;
					}
				}
				LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,lCurScore:%lld,lMinScore:%lld,bCanLeaveTable:%d,bLeaveTable:%d,bCanLeaveRoom:%d,bLeaveRoom:%d",
					GetRoomID(), GetTableID(), i, uid, lCurScore, lMinScore, bCanLeaveTable, bLeaveTable, bCanLeaveRoom, bLeaveRoom);
			}
		}
	}
}

void   CGameNiuniuTable::CheckPlayerScoreManyLeave()
{
	if (m_pHostRoom == NULL)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_pHostRoom:%p", GetRoomID(), GetTableID(), m_pHostRoom);
		return;
	}
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
		if (pPlayer != NULL)
		{
			int64 lCurScore = GetPlayerCurScore(pPlayer);
			int64 lMaxScore = m_pHostRoom->GetEnterMax();
			if (lCurScore >= lMaxScore)
			{
				uint32 uid = pPlayer->GetUID();
				bool bCanLeaveTable = CanLeaveTable(pPlayer);
				bool bLeaveTable = false;
				bool bCanLeaveRoom = false;
				bool bLeaveRoom = false;
				if (bCanLeaveTable)
				{
					bLeaveTable = LeaveTable(pPlayer);
					if (bLeaveTable)
					{
						bCanLeaveRoom = m_pHostRoom->CanLeaveRoom(pPlayer);
						if (bCanLeaveRoom)
						{
							bLeaveRoom = m_pHostRoom->LeaveRoom(pPlayer);
						}
						net::msg_leave_table_rep rep;
						rep.set_result(1);
						pPlayer->SendMsgToClient(&rep, net::S2C_MSG_LEAVE_TABLE_REP);
						m_szNoOperCount[i] = 0;
						m_szNoOperTrun[i] = 0;
					}
				}
				LOG_DEBUG("roomid:%d,tableid:%d,i:%d,uid:%d,lCurScore:%lld,lMaxScore:%lld,bCanLeaveTable:%d,bLeaveTable:%d,bCanLeaveRoom:%d,bLeaveRoom:%d",
					GetRoomID(), GetTableID(), i, uid, lCurScore, lMaxScore, bCanLeaveTable, bLeaveTable, bCanLeaveRoom, bLeaveRoom);
			}
		}
	}
}

bool CGameNiuniuTable::ProgressControlPalyer()
{
	bool bIsFalgControl = false;
	bool bIsControlPlayerIsReady = false;

	uint32 control_uid = m_tagControlPalyer.uid;
	uint32 game_count = m_tagControlPalyer.count;
	uint32 control_type = m_tagControlPalyer.type;

	if (control_uid != 0 && game_count>0 && control_type != GAME_CONTROL_CANCEL)
	{
		for (WORD i = 0; i<GAME_PLAYER; i++)
		{
			CGamePlayer *pPlayer = GetPlayer(i);
			if (pPlayer == NULL)
			{
				continue;
			}
			if (m_cbPlayStatus[i] == FALSE)
			{
				continue;
			}
			if (control_uid == pPlayer->GetUID())
			{
				bIsControlPlayerIsReady = true;
				break;
			}
		}
	}

	if (bIsControlPlayerIsReady && game_count>0 && control_type != GAME_CONTROL_CANCEL)
	{
		if (control_type == GAME_CONTROL_WIN)
		{
			bIsFalgControl = SetControlPalyerWin(control_uid);
		}
		if (control_type == GAME_CONTROL_LOST)
		{
			bIsFalgControl = SetControlPalyerLost(control_uid);
		}
		if (bIsFalgControl && m_tagControlPalyer.count>0)
		{
			//m_tagControlPalyer.count--;
			//if (m_tagControlPalyer.count == 0)
			//{
			//	m_tagControlPalyer.Init();
			//}
			if (m_pHostRoom != NULL)
			{
				m_pHostRoom->SynControlPlayer(GetTableID(), m_tagControlPalyer.uid, -1, m_tagControlPalyer.type);
			}
		}
	}

	LOG_DEBUG("roomid:%d,tableid:%d,control_uid:%d,game_count:%d,control_type:%d,bIsControlPlayerIsReady:%d,bIsFalgControl:%d",
		GetRoomID(), GetTableID(), control_uid, game_count, control_type, bIsControlPlayerIsReady, bIsFalgControl);

	return bIsFalgControl;
}

// 做牌发牌
void   CGameNiuniuTable::DispatchCard()
{
}

//游戏状态
bool    CGameNiuniuTable::IsUserPlaying(WORD wChairID)
{
	if (wChairID >= GAME_PLAYER)
	{
		return false;
	}
	ASSERT(wChairID<GAME_PLAYER);
	return (m_cbPlayStatus[wChairID]==TRUE)?true:false;    
}

//加注事件
bool    CGameNiuniuTable::OnUserAddScore(WORD wChairID, int64 lScore)
{

    net::msg_niuniu_place_jetton_rep rep;
    rep.set_jetton_score(lScore);

	bool bIsAddScoreTrue = false;
	for (int i = 0; i < JETTON_MULTIPLE_COUNT; i++)
	{
		if (m_lJettonMultiple[i] == lScore)
		{
			bIsAddScoreTrue = true;
			break;
		}
	}

	LOG_DEBUG("玩家加注 - roomid:%d,tableid:%d,uid:%d,wChairID:%d,lScore:%lld,m_wBankerUser:%d,bIsAddScoreTrue:%d",
		GetRoomID(), GetTableID(), GetPlayerID(wChairID), wChairID, lScore, m_wBankerUser, bIsAddScoreTrue);

    if(wChairID == m_wBankerUser || bIsAddScoreTrue == false)
	{
        rep.set_result(RESULT_CODE_FAIL);
        SendMsgToClient(wChairID,&rep,net::S2C_MSG_NIUNIU_PLACE_JETTON_REP);
        return false;
    }
    m_lTableScore[wChairID] = lScore;
    rep.set_result(net::RESULT_CODE_SUCCESS);
    SendMsgToClient(wChairID,&rep,net::S2C_MSG_NIUNIU_PLACE_JETTON_REP);

    net::msg_niuniu_place_jetton_broadcast broad;
    broad.set_jetton_score(lScore);
    broad.set_chairid(wChairID);

    SendMsgToAll(&broad,net::S2C_MSG_NIUNIU_PLACE_JETTON_BROADCAST);
    CheckOverTime();
	return true;
}

//申请庄家
bool    CGameNiuniuTable::OnUserApplyBanker(WORD wChairID,int32 score)
{
	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,score:%d,m_szApplyBanker:%d,",
		GetRoomID(), GetTableID(), wChairID, GetGameState(), GetPlayerID(wChairID), score, m_szApplyBanker[wChairID]);

	if (score < 0 || score >= NIU_MULTIPLE_COUNT)
	{
		return false;
	}
	if (m_szApplyBanker[wChairID] != FALSE)
	{
		return false;
	}
    if(score > 0)
	{
        m_szApplyBanker[wChairID] = TRUE;
    }
	else
	{
        m_szApplyBanker[wChairID] = 2;
    }

	m_lRobMultiple[wChairID] = score;

    net::msg_niuniu_apply_banker_rep msg;
    msg.set_chairid(wChairID);
    msg.set_score(score);
    msg.set_result(net::RESULT_CODE_SUCCESS);
    SendMsgToAll(&msg,net::S2C_MSG_NIUNIU_APPLY_BANKER_REP);

    //CheckOverTime();
    return true;
}

//摆牌
bool    CGameNiuniuTable::OnUserChangeCard(WORD wChairID,BYTE cards[])
{
    //LOG_DEBUG("玩家摆牌:%d",wChairID);
	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d",
		GetRoomID(), GetTableID(), wChairID, GetGameState(), GetPlayerID(wChairID));

    for(uint8 i=0;i<NIUNIU_CARD_NUM;++i)
    {
        bool isSame = false;
        for(uint8 j=0;j<NIUNIU_CARD_NUM;++j)
        {
           if(m_cbHandCardData[wChairID][i] == cards[j])
           {
               isSame = true;
               break;
           }
        }
		if (isSame == false)
		{
			LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d",
				GetRoomID(), GetTableID(), wChairID, GetGameState(), GetPlayerID(wChairID));

			return false;
		}
    }
    m_szShowCardState[wChairID] = TRUE;
    memcpy(m_cbChangeCardData[wChairID],cards, NIUNIU_CARD_NUM);

    SendChangeCard(wChairID,NULL);
	m_vecChangerChairID.emplace_back(wChairID);
    CheckOverTime();

	uint32 iCardCount = 0;
	for (uint8 i = 0; i < NIUNIU_CARD_NUM; ++i)
	{
		if (m_szShowCardState[i] == TRUE)
		{
			iCardCount++;
		}
	}
	if (iCardCount == GetPlayGameCount())
	{
		m_coolLogic.clearCool();
	}
    return true;
}

bool    CGameNiuniuTable::OnMasterUserOper(CGamePlayer* pPlayer, vector<BYTE> vecChairID, vector<BYTE> vecCardData)
{
	uint16 chairID = GetChairID(pPlayer);

	string strChairID;
	string strCardData;
	string strHandCard;

	net::msg_niuniu_recv_master_card_rep msg;
	for (uint32 i = 0; i < vecChairID.size(); i++)
	{
		msg.add_chairid(vecChairID[i]);

		strChairID += CStringUtility::FormatToString("%d ", vecChairID[i]);
	}

	for (uint32 i = 0; i < vecCardData.size(); i++)
	{
		strCardData += CStringUtility::FormatToString("0x%02X ", vecCardData[i]);
	}

	for (uint32 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			strHandCard += CStringUtility::FormatToString("i:%d,uid:%d,", i, GetPlayerID(i));
			for (uint32 j = 0; j < NIUNIU_CARD_NUM; ++j)
			{
				strHandCard += CStringUtility::FormatToString("0x%02X ", m_cbHandCardData[i][j]);
			}
		}
	}

	// 验证牌数量
	bool bIsCardCountSuccess = true;
	if (vecChairID.size() > 0 && vecCardData.size() > 0)
	{
		// 5  /1 == 5
		// 10 /2 == 5
		// 15 /3 == 5
		// 20 /4 == 5
		// 25 /5 == 5
		if (vecCardData.size() / vecChairID.size() == 5)
		{
			bIsCardCountSuccess = true;
		}
		else
		{
			bIsCardCountSuccess = false;
		}
	}
	else
	{
		bIsCardCountSuccess = false;
	}

	// 扑克不能重复不能错误
	bool bIsCardDataRepeat = false;
	for (uint32 i = 0; i < vecCardData.size(); i++)
	{
		BYTE cbTempCardData = vecCardData[i];
		if (m_gameLogic.IsValidCard(cbTempCardData) == false)
		{
			bIsCardDataRepeat = true;
			break;
		}
		for (uint32 j = 0; j < vecCardData.size(); j++)
		{
			if (i == j)
			{
				continue;
			}
			if (cbTempCardData == vecCardData[j])
			{
				bIsCardDataRepeat = true;
				break;
			}
		}
		if (bIsCardDataRepeat)
		{
			break;
		}
	}

	// 验证扑克牌数据是否在没换牌的玩家手上
	bool bIsCardDataConflict = false;
	for (uint32 i = 0; i < GAME_PLAYER; ++i)
	{
		auto find_chaidid = std::find(vecChairID.begin(), vecChairID.end(), i);
		if (find_chaidid != vecChairID.end())
		{
			// 正在换牌的不检查
			continue;
		}
		for (uint32 j = 0; j < NIUNIU_CARD_NUM; ++j)
		{
			BYTE cbTempCardData = m_cbHandCardData[i][j];
			auto find_carddata = std::find(vecCardData.begin(), vecCardData.end(), cbTempCardData);
			if (find_carddata != vecCardData.end())
			{
				bIsCardDataConflict = true;
				break;
			}
		}
		if (bIsCardDataConflict)
		{
			break;
		}
	}

	bool bIsMasterUser = GetIsMasterUser(pPlayer);

	bool bIsSuccessState = false;
	if (GetGameState() == TABLE_STATE_APBNIU_GAME_START)
	{
		bIsSuccessState = true;
	}
	if (GetGameState() == TABLE_STATE_APBNIU_APPLY_BRANKER)
	{
		bIsSuccessState = true;
	}
	if (GetGameState() == TABLE_STATE_APBNIU_MAKE_BRANKER)
	{
		bIsSuccessState = true;
	}
	if (GetGameState() == TABLE_STATE_APBNIU_PLACE_JETTON)
	{
		bIsSuccessState = true;
	}

	if (bIsSuccessState == false || bIsMasterUser == false || bIsCardCountSuccess == false || bIsCardDataRepeat == true || bIsCardDataConflict == true)
	{
		LOG_DEBUG("error - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,vecChairID.size:%d,vecCardData.size:%d,bIsSuccessState:%d,bIsMasterUser:%d,bIsCardCountSuccess:%d,bIsCardDataRepeat:%d,bIsCardDataConflict:%d,strChairID:%s,strCardData:%s,strHandCard:%s",
			GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), vecChairID.size(), vecCardData.size(), bIsSuccessState, bIsMasterUser, bIsCardCountSuccess, bIsCardDataRepeat, bIsCardDataConflict, strChairID.c_str(), strCardData.c_str(), strHandCard.c_str());

		vector<BYTE> vecRemainCardData;
		m_gameLogic.GetSubDataCard(m_cbHandCardData, vecRemainCardData);
		for (uint i = 0; i < vecRemainCardData.size(); i++)
		{
			msg.add_remain_cards(vecRemainCardData[i]);
		}
		msg.set_result(0);

		pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_RECV_MASTER_CARD_REP);
		return false;
	}

	// 验证完成可直接换牌

	LOG_DEBUG("sta - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,vecChairID.size:%d,vecCardData.size:%d,bIsSuccessState:%d,bIsMasterUser:%d,bIsCardCountSuccess:%d,bIsCardDataRepeat:%d,bIsCardDataConflict:%d,strChairID:%s,strCardData:%s,strHandCard:%s",
		GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), vecChairID.size(), vecCardData.size(), bIsSuccessState, bIsMasterUser, bIsCardCountSuccess, bIsCardDataRepeat, bIsCardDataConflict, strChairID.c_str(), strCardData.c_str(), strHandCard.c_str());

	for (uint32 i = 0; i < GAME_PLAYER; ++i)
	{
		for (uint32 k = 0; k < vecChairID.size(); k++)
		{
			if (i == vecChairID[k])
			{
				//换牌
				uint32 uCardIndex = k * 5;
				for (uint32 j = 0; j < NIUNIU_CARD_NUM; ++j)
				{
					m_cbHandCardData[i][j] = vecCardData[uCardIndex];
					uCardIndex++;
				}
			}
		}
	}

	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(m_cbHandCardData, vecRemainCardData);
	for (uint i = 0; i < vecRemainCardData.size(); i++)
	{
		msg.add_remain_cards(vecRemainCardData[i]);
	}
	msg.set_result(1);

	pPlayer->SendMsgToClient(&msg, net::S2C_MSG_NIUNIU_RECV_MASTER_CARD_REP);

	strHandCard.clear();

	for (uint32 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == TRUE)
		{
			strHandCard += CStringUtility::FormatToString("i:%d,uid:%d,", i, GetPlayerID(i));
			for (uint32 j = 0; j < NIUNIU_CARD_NUM; ++j)
			{
				strHandCard += CStringUtility::FormatToString("0x%02X ", m_cbHandCardData[i][j]);
			}
		}
	}

	m_bIsMasterUserOper = true;

	LOG_DEBUG("end - roomid:%d,tableid:%d,chairID:%d,status:%d,uid:%d,vecChairID.size:%d,vecCardData.size:%d,bIsSuccessState:%d,bIsMasterUser:%d,bIsCardCountSuccess:%d,bIsCardDataRepeat:%d,bIsCardDataConflict:%d,strChairID:%s,strCardData:%s,strHandCard:%s",
		GetRoomID(), GetTableID(), chairID, GetGameState(), GetPlayerID(chairID), vecChairID.size(), vecCardData.size(), bIsSuccessState, bIsMasterUser, bIsCardCountSuccess, bIsCardDataRepeat, bIsCardDataConflict, strChairID.c_str(), strCardData.c_str(), strHandCard.c_str());
	return true;
}

//发送摆牌
void    CGameNiuniuTable::SendChangeCard(WORD wChairID,CGamePlayer* pPlayer)
{
    net::msg_niuniu_change_card_rep msg;
    msg.set_oper_id(wChairID);
    msg.set_result(1);
    msg.set_card_type(m_cbTableCardType[wChairID]);
    for(uint8 i=0;i<NIUNIU_CARD_NUM;++i)
	{
        msg.add_cards(m_cbChangeCardData[wChairID][i]);
    }
    if(pPlayer == NULL)
	{
        SendMsgToAll(&msg, net::S2C_MSG_NIUNIU_CHANGE_CARD_REP);
    }
	else
	{
        pPlayer->SendMsgToClient(&msg,net::S2C_MSG_NIUNIU_CHANGE_CARD_REP);
    }
}

int32   CGameNiuniuTable::GetChangeCardNum()
{
    int32 num = 0;
    for(uint16 i=0;i<GAME_PLAYER;++i){
        if(m_szShowCardState[i] == TRUE)
            num++;
    }
    return num;
}

bool    CGameNiuniuTable::OnRobotInJetton()
{
	int64 uRemainTime = m_coolLogic.getCoolTick();
	int64 passtick = m_coolLogic.getPassTick();

	for (auto & item : m_vecRobotJetton)
	{
		if (passtick >= item.time && item.bflag == false)
		{
			item.count = GetRobotInJettonMultiple(item.chairID);
			bool bret = OnUserAddScore(item.chairID, item.count);

			item.bflag = true;
			LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,bret:%d,passtick:%lld,jettonScore:%d,",
				GetRoomID(), GetTableID(), item.chairID, GetPlayerID(item.chairID), bret, passtick, item.count);
		}
	}
    return true;
}

void CGameNiuniuTable::OnRobotJettonScore(uint16 chairID)
{
	if (chairID >= GAME_PLAYER)
	{
		return;
	}

	int64 lRobotJettonScore = GetRobotInJettonMultiple(chairID);
	bool bret = OnUserAddScore(chairID, lRobotJettonScore);
	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,bret:%d,lRobotJettonScore:%lld",
		GetRoomID(), GetTableID(), chairID, GetPlayerID(chairID), bret, lRobotJettonScore);
}

void CGameNiuniuTable::OnRobotRealJettonScore(uint16 chairID)
{
	if (chairID >= GAME_PLAYER)
	{
		return;
	}

	int64 lRobotJettonScore = m_lJettonMultiple[0];

	tagRobotOperItem OperItem;
	OperItem.uid = GetPlayerID(chairID);
	OperItem.chairID = chairID;
	OperItem.bflag = false;
	OperItem.count = lRobotJettonScore;

	int64 uRemainTime = m_coolLogic.getCoolTick();
	int64 passtick = m_coolLogic.getPassTick();
	int64 uMaxDelayTime = s_ApplyBrankerTime;
	int64 robotTime = m_vecRobotJetton.size() * 500 + g_RandGen.RandRange(1000, uRemainTime - 1500);
	if (robotTime >= (uRemainTime-500))
	{
		robotTime = uRemainTime - 1000;
	}
	OperItem.time = robotTime;

	m_vecRobotJetton.emplace_back(OperItem);

	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,robotTime:%d",GetRoomID(), GetTableID(), chairID, GetPlayerID(chairID), robotTime);
}

bool CGameNiuniuTable::OnTimeOutChangeCard()
{
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		if (m_szShowCardState[i] != FALSE)
		{
			continue;
		}
		CGamePlayer* pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			continue;
		}
		m_szNoOperCount[i]++;

		LOG_DEBUG("time_out - roomid:%d,tableid:%d,i:%d,status:%d,uid:%d,m_szNoOperCount:%d",
			GetRoomID(), GetTableID(), i, GetGameState(), GetPlayerID(i), m_szNoOperCount[i]);
		m_cbTimeOutShowCard[i] = TRUE;
		
		OnUserChangeCard(i, m_cbHandCardData[i]);
		//break;
	}
	return true;
}

bool CGameNiuniuTable::OnTimeOutApplyBanker()
{
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		if (m_szApplyBanker[i] != FALSE)
		{
			continue;
		}
		CGamePlayer* pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			continue;
		}
		int iApplyBrankerScore = s_iArrApplyMultiple[0];

		m_szNoOperCount[i]++;

		LOG_DEBUG("time_out - roomid:%d,tableid:%d,i:%d,status:%d,uid:%d,isrobot:%d,iApplyBrankerScore:%d,m_szNoOperCount:%d",
			GetRoomID(), GetTableID(), i, GetGameState(), GetPlayerID(i), pPlayer->IsRobot(), iApplyBrankerScore, m_szNoOperCount[i]);
		
		OnUserApplyBanker(i, iApplyBrankerScore);

		//OnRobotApplyBankerScore(i);

		//break;
	}
	return true;
}

bool    CGameNiuniuTable::OnRobotInApplyBanker()
{
	int64 uRemainTime = m_coolLogic.getCoolTick();
	int64 passtick = m_coolLogic.getPassTick();

	for (auto & item : m_vecRobotApplyBanker)
	{
		if (passtick>= item.time && item.bflag == false)
		{
			item.count = GetRobotInApplyMultiple(item.chairID);
			bool bret = OnUserApplyBanker(item.chairID, item.count);
			item.bflag = true;
			LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,bret:%d,passtick:%lld,ApplyScore:%d,",
				GetRoomID(), GetTableID(), item.chairID, GetPlayerID(item.chairID), bret, passtick, item.count);
		}
	}
    return true;
}

int64 CGameNiuniuTable::GetRobotInApplyMultiple(uint16 chairID)
{
	int64 count = s_iArrApplyMultiple[0];
	if (chairID >= GAME_PLAYER)
	{
		return count;
	}
	std::pair<int, int64> maxItem{ 255, 0 };
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		if (m_szApplyBanker[i] != FALSE)
		{
			if (maxItem.second < m_lRobMultiple[i])
			{
				maxItem.second = m_lRobMultiple[i];
				maxItem.first = i;
			}
		}
	}
	//const static int s_iArrApplyMultiple[NIU_MULTIPLE_COUNT] = { 0, 1, 2, 3, 4 };
	string strRemainItem;
	std::vector<int> vecRemainItem;
	std::vector<int> vecRemainIndex;
	if (maxItem.first != 255)
	{
		for (int i = 0; i < NIU_MULTIPLE_COUNT; i++)
		{
			if (s_iArrApplyMultiple[i] == maxItem.second)
			{
				if (i != 0)
				{
					vecRemainItem.push_back(m_iApplyBankerPro[0]);
					vecRemainIndex.push_back(0);
				}
				for (int j = i; j < NIU_MULTIPLE_COUNT; j++)
				{
					vecRemainItem.push_back(m_iApplyBankerPro[j]);
					vecRemainIndex.push_back(j);
				}
				break;
			}
		}
		uint32 iProIndex = 0;
		if (vecRemainItem.size() == 0)
		{
			iProIndex = g_RandGen.RandRange(0, NIU_MULTIPLE_COUNT - 1);
			count = s_iArrApplyMultiple[iProIndex];
		}
		else
		{
			strRemainItem += CStringUtility::FormatToString("strRemainItem:");
			int iSumPro = 0;
			for (uint32 i = 0; i < vecRemainItem.size(); i++)
			{
				iSumPro += vecRemainItem[i];

				strRemainItem += CStringUtility::FormatToString("%d ", vecRemainItem[i]);
			}
			strRemainItem += CStringUtility::FormatToString("iSumPro:%d ", iSumPro);
			if (iSumPro <= 0)
			{
				iProIndex = 0;// g_RandGen.RandRange(0, NIU_MULTIPLE_COUNT - 1);
				count = s_iArrApplyMultiple[iProIndex];
			}
			else
			{
				int iRandNum = g_RandGen.RandRange(0, iSumPro);
				strRemainItem += CStringUtility::FormatToString("iRandNum:%d ", iRandNum);
				for (; iProIndex < vecRemainItem.size(); iProIndex++)
				{
					if (vecRemainItem[iProIndex]==0)
					{
						continue;
					}
					if (iRandNum <= vecRemainItem[iProIndex])
					{
						break;
					}
					else
					{
						iRandNum -= vecRemainItem[iProIndex];
					}
				}
				strRemainItem += CStringUtility::FormatToString("iProIndex:%d ", iProIndex);
				if (iProIndex >= vecRemainItem.size())
				{
					iProIndex = 0;
				}
				int iRandIndex = vecRemainIndex[iProIndex];
				if (iRandIndex >= 0 && iRandIndex < NIU_MULTIPLE_COUNT)
				{
					count = s_iArrApplyMultiple[iRandIndex];
				}
				strRemainItem += CStringUtility::FormatToString("iProIndex2:%d ", iProIndex);
				strRemainItem += CStringUtility::FormatToString("iRandIndex:%d ", iProIndex);
				strRemainItem += CStringUtility::FormatToString("count:%d ", count);
			}
		}
	}
	else
	{
		strRemainItem += CStringUtility::FormatToString("m_iApplyBankerPro:");

		int iSumPro = 0;
		for (int i = 0; i < NIU_MULTIPLE_COUNT; i++)
		{
			iSumPro += m_iApplyBankerPro[i];
			strRemainItem += CStringUtility::FormatToString("%d ", m_iApplyBankerPro[i]);

		}
		strRemainItem += CStringUtility::FormatToString("iSumPro:%d ", iSumPro);

		int iProIndex = 0;
		if (iSumPro <= 0)
		{
			iProIndex = g_RandGen.RandRange(0, NIU_MULTIPLE_COUNT - 1);
		}
		else
		{
			int iRandNum = g_RandGen.RandRange(0, iSumPro);
			strRemainItem += CStringUtility::FormatToString("iRandNum:%d ", iRandNum);

			for (; iProIndex < NIU_MULTIPLE_COUNT; iProIndex++)
			{
				if (m_iApplyBankerPro[iProIndex] == 0)
				{
					continue;
				}
				if (iRandNum <= m_iApplyBankerPro[iProIndex])
				{
					break;
				}
				else
				{
					iRandNum -= m_iApplyBankerPro[iProIndex];
				}
			}
			strRemainItem += CStringUtility::FormatToString("iProIndex:%d ", iProIndex);

			if (iProIndex >= NIU_MULTIPLE_COUNT)
			{
				iProIndex = 0;
			}

		}
		strRemainItem += CStringUtility::FormatToString("iProIndex2:%d ", iProIndex);
		count = s_iArrApplyMultiple[iProIndex];
	}
	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,count:%d,maxItem_first:%d,second:%d,vecRemainItem.size:%d,strRemainItem:%s",
		GetRoomID(), GetTableID(), chairID, GetPlayerID(chairID), count, maxItem.first, maxItem.second, vecRemainItem.size(), strRemainItem.c_str());
	return count;
}

int64 CGameNiuniuTable::GetRobotInJettonMultiple(uint16 chairID)
{
	int64 count = m_lJettonMultiple[0];
	if (chairID >= GAME_PLAYER)
	{
		return count;
	}

	int iSumPro = 0;
	for (int i = 0; i < JETTON_MULTIPLE_COUNT; i++)
	{
		iSumPro += m_iRobotJettonPro[i];
	}
	int iProIndex = 0;
	if (iSumPro <= 0)
	{
		iProIndex = g_RandGen.RandRange(0, JETTON_MULTIPLE_COUNT - 1);
	}
	else
	{
		int iRandNum = g_RandGen.RandRange(0, iSumPro);
		for (; iProIndex < JETTON_MULTIPLE_COUNT; iProIndex++)
		{
			if (m_iRobotJettonPro[iProIndex] == 0)
			{
				continue;
			}
			if (iRandNum <= m_iRobotJettonPro[iProIndex])
			{
				break;
			}
			else
			{
				iRandNum -= m_iRobotJettonPro[iProIndex];
			}
		}
		if (iProIndex >= JETTON_MULTIPLE_COUNT)
		{
			iProIndex = 0;
		}
	}

	count = m_lJettonMultiple[iProIndex];

	return count;
}

void CGameNiuniuTable::OnRobotApplyBankerScore(uint16 chairID)
{
	if (chairID >= GAME_PLAYER)
	{
		return;
	}

	int iApplyBrankerScore = GetRobotInApplyMultiple(chairID);

	bool bret = OnUserApplyBanker(chairID, iApplyBrankerScore);

	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,bret:%d,ApplyScore:%d",
		GetRoomID(), GetTableID(), chairID, GetPlayerID(chairID), bret, iApplyBrankerScore);
}

void CGameNiuniuTable::OnRobotRealApplyBankerScore(uint16 chairID)
{
	if (chairID >= GAME_PLAYER)
	{
		return;
	}

	tagRobotOperItem OperItem;
	OperItem.uid = GetPlayerID(chairID);
	OperItem.chairID = chairID;
	OperItem.bflag = false;
	OperItem.count = s_iArrApplyMultiple[0];
	
	
	int64 uRemainTime = m_coolLogic.getCoolTick();
	int64 passtick = m_coolLogic.getPassTick();
	int64 uMaxDelayTime = s_ApplyBrankerTime;
	int64 robotTime = m_vecRobotApplyBanker.size() * 500 + g_RandGen.RandRange(1000, uRemainTime-1500);
	if (robotTime>= (uRemainTime-500))
	{
		robotTime = uRemainTime - 1000;
	}
	OperItem.time = robotTime;

	m_vecRobotApplyBanker.emplace_back(OperItem);

	LOG_DEBUG("roomid:%d,tableid:%d,chairID:%d,uid:%d,robotTime:%lld",GetRoomID(), GetTableID(), chairID, GetPlayerID(chairID), robotTime);
}

bool    CGameNiuniuTable::OnRobotReady()
{
    if(m_coolLogic.getCoolTick() < 1000)
	{
        ReadyAllRobot();
        return true;
    }
	if (!m_coolRobot.isTimeOut())
	{
		return false;
	}
    for(uint32 i=0;i<m_vecPlayers.size();++i)
	{
        CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
        if(pPlayer != NULL && pPlayer->IsRobot())
        {
            if(m_vecPlayers[i].readyState == 0)
			{
                PlayerReady(pPlayer);
                break;
            }
        }
    }
    return true;
}

bool    CGameNiuniuTable::OnRobotChangeCard()
{	
    for(uint32 i=0;i<m_vecPlayers.size();++i)
    {
        CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
        if(pPlayer != NULL && pPlayer->IsRobot())
        {
            if(m_cbPlayStatus[i] == TRUE && m_szShowCardState[i] == FALSE)
			{
                OnUserChangeCard(i,m_cbHandCardData[i]);
                break;
            }
        }
    }
    //m_coolRobot.beginCooling(g_RandGen.RandRange(500,1000));
    return true;
}
void    CGameNiuniuTable::CheckAddRobot()
{
	if (m_pHostRoom->GetRobotCfg() == 0 || !m_coolRobot.isTimeOut())
	{
		return;
	}

	for (uint32 i = 0; i < m_vecPlayers.size(); ++i)
	{
		CGamePlayer* pPlayer = m_vecPlayers[i].pPlayer;
		if (pPlayer != NULL && !pPlayer->IsRobot())
		{
			int freeCount = GetFreeChairNum();
			int minCount = 1;
			if (freeCount >= 2)
			{
				minCount = 2;
			}
			if (freeCount > 1)
			{
				if (freeCount < GAME_PLAYER)
				{
					int iRobotCount = g_RandGen.RandRange(minCount, freeCount);
					CRobotMgr::Instance().RequestXRobot(iRobotCount, this);
				}
			}
			m_coolRobot.beginCooling(g_RandGen.RandRange(3000, 5000));
			return;
		}
	}	
}

void    CGameNiuniuTable::SetRobotThinkTime()
{
    if(GetGameState() == TABLE_STATE_APBNIU_FREE) 
	{
        m_coolRobot.beginCooling(g_RandGen.RandRange(1000, 2000));
    }
	else
	{
        m_coolRobot.beginCooling(g_RandGen.RandRange(2000, 3000));
    }
}

bool CGameNiuniuTable::HaveOverWelfareMaxJettonScore(int64 newmaxjetton)
{
	for (WORD wChairID = 0; wChairID<GAME_PLAYER; wChairID++)
	{
		if (m_cbPlayStatus[wChairID] == FALSE)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(wChairID);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot())
		{
			continue;
		}
		if (pPlayer->IsNoviceWelfare()==false)
		{
			continue;
		}
		if (m_lTableScore[wChairID] >= newmaxjetton)
		{
			return true;
		}
	}
	return false;
}

CGamePlayer* CGameNiuniuTable::HaveWelfareNovicePlayer()
{
	int count = 0;
	CGamePlayer * pTempPlayer = NULL;
	for (WORD wChairID = 0; wChairID<GAME_PLAYER; wChairID++)
	{
		if (m_cbPlayStatus[wChairID] == FALSE)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(wChairID);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot())
		{
			continue;
		}
		if (pPlayer->IsNoviceWelfare() == false)
		{
			return NULL;
		}
		else
		{
			pTempPlayer = pPlayer;
			count++;
		}
	}
	if (count == 1)
	{
		return pTempPlayer;
	}
	return NULL;
}

// 结算分数
int64   CGameNiuniuTable::GetNoviceWinScore(uint32 noviceuid)
{
	int64 lNoviceWinScore = 0;
	int64 lWinScore[GAME_PLAYER] = { 0 };
	BYTE  winMultiple[GAME_PLAYER] = { 0 };
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == m_wBankerUser)
		{
			continue;
		}
		bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[m_wBankerUser], NIUNIU_CARD_NUM, m_cbHandCardData[i], NIUNIU_CARD_NUM, winMultiple[i]) == 1 ? true : false;
		int64 lLostWinFlag = 0;
		if (bWin)
		{
			lLostWinFlag = 1;
		}
		else
		{
			lLostWinFlag = -1;
		}
		int64 lLostWinScore = lLostWinFlag * GetBaseScore() * GetPlayerAddScore(i) * GetPlayerRobMultiple(i) * winMultiple[i];
		lWinScore[i] += lLostWinScore;
		lWinScore[m_wBankerUser] -= lLostWinScore;		
	}
		
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		CGamePlayer * pGamePlayer = GetPlayer(i);
		if (pGamePlayer != NULL && pGamePlayer->GetUID() == noviceuid)
		{
			lNoviceWinScore = lWinScore[i];
			break;
		}
	}
	return lNoviceWinScore;
}

// 机器人当庄赢金币
bool CGameNiuniuTable::SetRobotBrankerCanWinScore()
{
	if (m_wBankerUser >= GAME_PLAYER)
	{
		LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d",
			GetRoomID(), GetTableID(), m_wBankerUser);
		return false;
	}

	bool bRobotIsRranker = false;
	CGamePlayer * pBrankerPlayer = GetPlayer(m_wBankerUser);
	if (pBrankerPlayer != NULL)
	{
		bRobotIsRranker = pBrankerPlayer->IsRobot();
	}
	if (bRobotIsRranker == false)
	{
		// 机器人不是庄家退出
		LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,pBrankerPlayer:%p",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), pBrankerPlayer);
		return false;
	}

	// 拷贝手牌
	BYTE cbTempHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbTempHandCard, m_cbHandCardData, sizeof(cbTempHandCard));
	BYTE cbBrankerHandCardData[NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbBrankerHandCardData, cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);

	bool bIsRobotBrankerMaxCardType = true;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		BYTE cbTempWinMultiple = 0;
		int iCompResult = m_gameLogic.CompareCard(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM, cbTempHandCard[iPlayerIndex], NIUNIU_CARD_NUM, cbTempWinMultiple);
		if (iCompResult == 1)
		{
			bIsRobotBrankerMaxCardType = false;
			break;
		}
	}
	if (bIsRobotBrankerMaxCardType == true)
	{
		// 庄家机器人最大则肯定能赢
		LOG_DEBUG("ismax_robot_card_type - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser));

		return true;
	}

	//获取剩余
	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(cbTempHandCard, vecRemainCardData);
	vector<BYTE> vecSpecialTypeCardData[CT_SPECIAL_MAX_TYPE];
	for (uint32 iCardIndex = 0; iCardIndex < vecRemainCardData.size(); iCardIndex++)
	{
		cbBrankerHandCardData[4] = vecRemainCardData[iCardIndex];
		BYTE cbTempCardType = m_gameLogic.GetCardType(cbBrankerHandCardData, NIUNIU_CARD_NUM);
		if (cbTempCardType<CT_SPECIAL_MAX_TYPE)
		{
			vecSpecialTypeCardData[cbTempCardType].push_back(vecRemainCardData[iCardIndex]);
		}
	}
	
	// 获取庄家牌型和闲家最大牌型
	BYTE cbBrankerCardType = m_gameLogic.GetCardType(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);
	BYTE cbPlayerMaxCardType = CT_ERROR;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(iPlayerIndex);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot())
		{
			continue;
		}
		BYTE cbTempCardType = m_gameLogic.GetCardType(cbTempHandCard[iPlayerIndex], NIUNIU_CARD_NUM);
		if (cbTempCardType > cbPlayerMaxCardType)
		{
			cbPlayerMaxCardType = cbTempCardType;
		}
	}
	if (cbBrankerCardType >= CT_SPECIAL_BOMEBOME)
	{
		// 庄家机器人牌型已经是最大
		LOG_DEBUG("ismax_card_type - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,cbBrankerCardType:%d,cbPlayerMaxCardType:%d,",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), cbBrankerCardType, cbPlayerMaxCardType);

		return false;
	}

	// 获取所有赢金币的牌型和扑克牌
	BYTE cbLoopHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbLoopHandCard, cbTempHandCard, sizeof(cbLoopHandCard));
	
	vector<BYTE> vecWinScoreCardType;
	vector<BYTE> vecWinScoreTypeCardData[CT_SPECIAL_MAX_TYPE];
	
	for (BYTE cbCardTypeIndex = cbBrankerCardType + 1; cbCardTypeIndex < CT_SPECIAL_MAX_TYPE; cbCardTypeIndex++)
	{
		if (vecSpecialTypeCardData[cbCardTypeIndex].size() > 0)
		{
			BYTE cbTempLoopCardData = vecSpecialTypeCardData[cbCardTypeIndex][0];
			cbLoopHandCard[m_wBankerUser][4] = cbTempLoopCardData;
			// 计算此时的得分
			int64 lLoopWinScore[GAME_PLAYER] = { 0 };
			BYTE cbLoopWinMultiple[GAME_PLAYER] = { 0 };
			for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
			{
				if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
				{
					continue;
				}
				int iCompResult = m_gameLogic.CompareCard(cbLoopHandCard[m_wBankerUser], NIUNIU_CARD_NUM, cbLoopHandCard[iPlayerIndex], NIUNIU_CARD_NUM, cbLoopWinMultiple[iPlayerIndex]);
				int64 lLostWinFlag = 0;
				if (iCompResult == 1)
				{
					lLostWinFlag = 1;
				}
				else
				{
					lLostWinFlag = -1;
				}
				int64 lLostWinScore = lLostWinFlag * GetBaseScore() * GetPlayerAddScore(iPlayerIndex) * GetPlayerRobMultiple(iPlayerIndex) * cbLoopWinMultiple[iPlayerIndex];
				lLoopWinScore[iPlayerIndex] += lLostWinScore;
				lLoopWinScore[m_wBankerUser] -= lLostWinScore;
			}

			int64 lLoopBrankerWinPlayerScore = 0;
			for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
			{
				if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
				{
					continue;
				}
				CGamePlayer * pPlayer = GetPlayer(iPlayerIndex);
				if (pPlayer == NULL)
				{
					continue;
				}
				if (pPlayer->IsRobot())
				{
					continue;
				}
				lLoopBrankerWinPlayerScore -= lLoopWinScore[iPlayerIndex];
			}
			if (lLoopBrankerWinPlayerScore > 0)
			{
				// 机器人当庄的时候，庄家赢真实玩家的金币就可以
				vecWinScoreCardType.push_back(cbCardTypeIndex);
				vecWinScoreTypeCardData[cbCardTypeIndex].push_back(cbTempLoopCardData);
				//for (uint32 uLoopCardIndex = 0; uLoopCardIndex < vecSpecialTypeCardData[cbCardTypeIndex].size(); uLoopCardIndex++)
				//{
				//	vecWinScoreTypeCardData[cbCardTypeIndex].push_back(vecSpecialTypeCardData[cbCardTypeIndex][uLoopCardIndex]);
				//}
			}
		}
	}
	
	//如果机器人有能赢金币
	if (vecWinScoreCardType.size()>0)
	{
		//计算概率总和
		int iSumCardTypePro = 0;
		for (uint32 iProIndex = 0; iProIndex < vecWinScoreCardType.size(); iProIndex++)
		{
			iSumCardTypePro = m_iArrDispatchCardPro[vecWinScoreCardType[iProIndex]];
		}
		//获取能赢概率的每个权值
		vector<pair<int,int>> vecDispatchCardPro;
		for (BYTE cbFindProIndex = CT_POINT; cbFindProIndex < CT_SPECIAL_MAX_TYPE; cbFindProIndex++)
		{
			auto iter_win_card_type = std::find(vecWinScoreCardType.begin(), vecWinScoreCardType.end(), cbFindProIndex);
			if (iter_win_card_type != vecWinScoreCardType.end())
			{
				std::pair<int, int> tempPairPro;
				tempPairPro.first = cbFindProIndex;
				tempPairPro.second = m_iArrDispatchCardPro[cbFindProIndex];

				vecDispatchCardPro.push_back(tempPairPro);
			}
		}
		//根据能赢牌型的每个权值计算概率，如果为0则随机一个
		int iRandCardTypeIndex = 0;
		if (iSumCardTypePro == 0)
		{
			iRandCardTypeIndex = g_RandGen.RandRange(0, vecWinScoreCardType.size() - 1);
		}
		else
		{
			int iRandNum = g_RandGen.RandRange(0, iSumCardTypePro);

			int iSubProIndex = 0;
			for (; iSubProIndex < (int)vecDispatchCardPro.size(); iSubProIndex++)
			{
				if (iRandNum <= vecDispatchCardPro[iSubProIndex].second)
				{
					break;
				}
				else
				{
					iRandNum -= vecDispatchCardPro[iSubProIndex].second;
				}
			}
			iRandCardTypeIndex = vecDispatchCardPro[iSubProIndex].first;
		}
		BYTE cbRealCardType = iRandCardTypeIndex;// vecWinScoreCardType[iRandCardTypeIndex];

		if (vecWinScoreTypeCardData[cbRealCardType].size()>0)
		{
			// 给庄家最后一张牌赋值
			//int iRealCardTypeIndex = g_RandGen.RandRange(0, vecSpecialTypeCardData[cbRealCardType].size() - 1);
			//BYTE cbRealCardData = vecSpecialTypeCardData[cbRealCardType][iRealCardTypeIndex];
			BYTE cbRealCardData = vecWinScoreTypeCardData[cbRealCardType][0];
			m_cbHandCardData[m_wBankerUser][4] = cbRealCardData;
			
			LOG_DEBUG("winscore_card_type_success - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecWinScoreCardType_size:%d,cbRealCardType:%d,cbRealCardData:0x%02X",
				GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecWinScoreCardType.size(), cbRealCardType, cbRealCardData);

			return true;
		}
		else
		{
			LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecWinScoreCardType_size:%d,cbRealCardType:%d",
				GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecWinScoreCardType.size(), cbRealCardType);
			// // 这手牌没有能够给机器人赢筹码的，需要重新换手牌
			return false;
		}
	}
	else
	{

	}
	// 剩余的牌不能凑出机器人能赢金币的手牌，重新换个手牌
	return false;
}

// 真实玩家当庄输金币
bool CGameNiuniuTable::SetPlayerBrankerLostScore()
{
	if (m_wBankerUser >= GAME_PLAYER)
	{
		LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d",
			GetRoomID(), GetTableID(), m_wBankerUser);

		return false;
	}

	//获取机器椅子号
	vector<BYTE> vecRobotChairID;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(iPlayerIndex);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot())
		{
			vecRobotChairID.push_back(iPlayerIndex);
		}
	}
	if (vecRobotChairID.size() == 0)
	{
		// 没有机器人不控制
		LOG_DEBUG("not_have_robot - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecRobotChairID.size:%d",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecRobotChairID.size());

		return true;
	}
	//判断有效庄家
	bool bPlayerIsRranker = false;
	CGamePlayer * pBrankerPlayer = GetPlayer(m_wBankerUser);
	if (pBrankerPlayer != NULL)
	{
		bPlayerIsRranker = pBrankerPlayer->IsRobot();
	}
	if (bPlayerIsRranker == true)
	{
		// 真实用户不是庄家退出
		LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,pBrankerPlayer:%p",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), pBrankerPlayer);

		return true;
	}

	// 拷贝手牌
	BYTE cbTempHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbTempHandCard, m_cbHandCardData, sizeof(cbTempHandCard));
		
	BYTE cbBrankerHandCardData[NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbBrankerHandCardData, cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);

	bool bIsPlayerBrankerMinCardType = true;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		BYTE cbTempWinMultiple = 0;
		int iCompResult = m_gameLogic.CompareCard(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM, cbTempHandCard[iPlayerIndex], NIUNIU_CARD_NUM, cbTempWinMultiple);
		if (iCompResult == -1)
		{
			bIsPlayerBrankerMinCardType = false;
			break;
		}
	}
	if (bIsPlayerBrankerMinCardType == true)
	{
		// 真实玩家庄家最小则机器人肯定赢金币
		LOG_DEBUG("ismin_player_card_type - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser));
		return true;
	}
	
	// 如果机器人能够赢金币 就不需要再换牌
	int64 lExistWinScore[GAME_PLAYER] = { 0 };
	BYTE cbExistMultiple[GAME_PLAYER] = { 0 };
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		int iCompResult = m_gameLogic.CompareCard(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM, cbTempHandCard[iPlayerIndex], NIUNIU_CARD_NUM, cbExistMultiple[iPlayerIndex]);
		int64 lLostWinFlag = 0;
		if (iCompResult == 1)
		{
			lLostWinFlag = 1;
		}
		else
		{
			lLostWinFlag = -1;
		}
		int64 lLostWinScore = lLostWinFlag * GetBaseScore() * GetPlayerAddScore(iPlayerIndex) * GetPlayerRobMultiple(iPlayerIndex) * cbExistMultiple[iPlayerIndex];
		lExistWinScore[iPlayerIndex] += lLostWinScore;
		lExistWinScore[m_wBankerUser] -= lLostWinScore;
	}
	//检查机器人是否在此时能够赢取筹码
	int64 lExistAllRobotWinScore = 0;
	for (uint32 uRobotChairLoopIndex = 0; uRobotChairLoopIndex < vecRobotChairID.size(); uRobotChairLoopIndex++)
	{
		BYTE cbTempRobotChairID = vecRobotChairID[uRobotChairLoopIndex];
		lExistAllRobotWinScore += lExistWinScore[cbTempRobotChairID];
	}
	if (lExistAllRobotWinScore > 0)
	{
		//机器人已经赢 不需要再换
		LOG_DEBUG("winscore_card_type_error - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,lExistAllRobotWinScore:%lld",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), lExistAllRobotWinScore);

		return true;
	}
	// 否则给机器人换牌	


	//获取剩余扑克
	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(cbTempHandCard, vecRemainCardData);

	//机器人获取将要得到的每个类型
	vector<BYTE> vecSpecialTypeCardData[GAME_PLAYER][CT_SPECIAL_MAX_TYPE];
	BYTE cbLoopHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbLoopHandCard, cbTempHandCard, sizeof(cbLoopHandCard));
	for (uint32 iRobotIndex = 0; iRobotIndex < vecRobotChairID.size(); iRobotIndex++)
	{
		BYTE cbTempRobotChairID = vecRobotChairID[iRobotIndex];
		for (uint32 iCardIndex = 0; iCardIndex < vecRemainCardData.size(); iCardIndex++)
		{
			cbLoopHandCard[cbTempRobotChairID][4] = vecRemainCardData[iCardIndex];
			BYTE cbTempCardType = m_gameLogic.GetCardType(cbLoopHandCard[cbTempRobotChairID], NIUNIU_CARD_NUM);
			if (cbTempCardType<CT_SPECIAL_MAX_TYPE)
			{
				vecSpecialTypeCardData[cbTempRobotChairID][cbTempCardType].push_back(vecRemainCardData[iCardIndex]);
			}
		}
	}

	//计算每个机器人的牌型
	BYTE cbRobotCardType[GAME_PLAYER] = { 0 };
	for (uint32 iRobotChairIndex = 0; iRobotChairIndex < vecRobotChairID.size(); iRobotChairIndex++)
	{
		BYTE cbTempRobotChairID = vecRobotChairID[iRobotChairIndex];
		BYTE cbTempRobotCardType = m_gameLogic.GetCardType(cbTempHandCard[cbTempRobotChairID], NIUNIU_CARD_NUM);
		if (cbTempRobotCardType < CT_SPECIAL_MAX_TYPE)
		{
			cbRobotCardType[cbTempRobotChairID] = cbTempRobotCardType;
		}
	}

	//每个机器人牌型逐渐上升直到最后机器人总的能赢分
	uint32 uRobotCurCardType[GAME_PLAYER] = { 0 };
	uint32 uRobotLoopCardType[GAME_PLAYER] = { 0 };
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		uRobotCurCardType[iPlayerIndex] = cbRobotCardType[iPlayerIndex];
		uRobotLoopCardType[iPlayerIndex] = cbRobotCardType[iPlayerIndex];
	}
	bool bIsAllRobotIsMaxType = false;
	int iLoopIndex = 0;
	do
	{
		BYTE cbCaclHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
		memcpy(cbCaclHandCard, cbTempHandCard, sizeof(cbCaclHandCard));

		vector<BYTE> vecReadySwapCardData;
		for (uint32 iRobotChairIndex = 0; iRobotChairIndex < vecRobotChairID.size(); iRobotChairIndex++)
		{
			BYTE cbTempRobotChairID = vecRobotChairID[iRobotChairIndex];
			if (uRobotCurCardType[cbTempRobotChairID] < CT_SPECIAL_BOMEBOME)
			{
				uRobotCurCardType[cbTempRobotChairID]++;

				uint32 uTempRobotCardType = uRobotCurCardType[cbTempRobotChairID];
				for (; uTempRobotCardType <= CT_SPECIAL_BOMEBOME; uTempRobotCardType++)
				{
					uint32 uTempRobotCurCardSize = vecSpecialTypeCardData[cbTempRobotChairID][uTempRobotCardType].size();
					if (uTempRobotCurCardSize > 0)
					{
						BYTE cbReadySwapCardData = vecSpecialTypeCardData[cbTempRobotChairID][uTempRobotCardType][0];
						auto iter_swap_find = find(vecReadySwapCardData.begin(), vecReadySwapCardData.end(), cbReadySwapCardData);
						if (iter_swap_find == vecReadySwapCardData.end())
						{
							cbCaclHandCard[cbTempRobotChairID][4] = cbReadySwapCardData;
							vecReadySwapCardData.push_back(cbReadySwapCardData);
						}
						uRobotCurCardType[cbTempRobotChairID] = uTempRobotCardType;
					}
					uRobotLoopCardType[cbTempRobotChairID] = uTempRobotCardType;
				}
			}
		}
		vecReadySwapCardData.clear();
		// 计算此时机器人的手牌是否能够赢金币
		int64 lCaclWinScore[GAME_PLAYER] = { 0 };
		BYTE cbCaclWinMultiple[GAME_PLAYER] = { 0 };
		for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
		{
			if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
			{
				continue;
			}
			int iCompResult = m_gameLogic.CompareCard(cbCaclHandCard[m_wBankerUser], NIUNIU_CARD_NUM, cbCaclHandCard[iPlayerIndex], NIUNIU_CARD_NUM, cbCaclWinMultiple[iPlayerIndex]);
			int64 lLostWinFlag = 0;
			if (iCompResult == 1)
			{
				lLostWinFlag = 1;
			}
			else
			{
				lLostWinFlag = -1;
			}
			int64 lLostWinScore = lLostWinFlag * GetBaseScore() * GetPlayerAddScore(iPlayerIndex) * GetPlayerRobMultiple(iPlayerIndex) * cbCaclWinMultiple[iPlayerIndex];
			lCaclWinScore[iPlayerIndex] += lLostWinScore;
			lCaclWinScore[m_wBankerUser] -= lLostWinScore;
		}
		//检查机器人是否在此时能够赢取筹码
		int64 lCaclAllRobotWinScore = 0;
		for (uint32 uRobotChairLoopIndex = 0; uRobotChairLoopIndex < vecRobotChairID.size(); uRobotChairLoopIndex++)
		{
			BYTE cbTempRobotChairID = vecRobotChairID[uRobotChairLoopIndex];
			lCaclAllRobotWinScore += lCaclWinScore[cbTempRobotChairID];
		}
		if (lCaclAllRobotWinScore > 0)
		{
			// 给每个机器人换手牌
			for (uint32 iRobotChairIndex = 0; iRobotChairIndex < vecRobotChairID.size(); iRobotChairIndex++)
			{
				BYTE cbTempRobotChairID = vecRobotChairID[iRobotChairIndex];

				m_cbHandCardData[cbTempRobotChairID][4] = cbCaclHandCard[cbTempRobotChairID][4];
			}

			LOG_DEBUG("winscore_card_type_success - roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,iLoopIndex:%d,lCaclAllRobotWinScore:%lld",
				GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), iLoopIndex, lCaclAllRobotWinScore);

			return true;
		}
		// 否则每个机器人再重新上升一个牌型
		for (uint32 iRobotChairIndex = 0; iRobotChairIndex < vecRobotChairID.size(); iRobotChairIndex++)
		{
			BYTE cbTempRobotChairID = vecRobotChairID[iRobotChairIndex];
			if (uRobotLoopCardType[cbTempRobotChairID] >= CT_SPECIAL_BOMEBOME)
			{
				bIsAllRobotIsMaxType = true;
			}
		}
		iLoopIndex++;
		if (iLoopIndex >= MAX_RAND_LOOP_COUNT)
		{
			break;
		}
		if (bIsAllRobotIsMaxType)
		{
			break;
		}
	// 每个机器人的牌型是最大的时候退出循环
	} while (true);

	// 剩余的牌不能凑出机器人能赢金币的手牌，重新换个手牌
	return false;
}

// 如果机器人当庄，降真实玩家牌型
bool CGameNiuniuTable::OnRobotBrankerSubPlayerCardType()
{
	if (m_wBankerUser >= GAME_PLAYER)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d", GetRoomID(), GetTableID(), m_wBankerUser);
		return false;
	}
	vector<BYTE> vecPlayerChairID;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE || iPlayerIndex == m_wBankerUser)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(iPlayerIndex);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot() == false)
		{
			vecPlayerChairID.push_back(iPlayerIndex);
		}
	}
	if (vecPlayerChairID.size() == 0)
	{
		// 没有真实玩家不控制
		LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecPlayerChairID.size:%d",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecPlayerChairID.size());
		return false;
	}
	BYTE cbTempHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbTempHandCard, m_cbHandCardData, sizeof(cbTempHandCard));


	//计算每个真实玩家的牌型
	BYTE cbPlayerCardType[GAME_PLAYER] = { 0 };
	for (uint32 iPlayerChairIndex = 0; iPlayerChairIndex < vecPlayerChairID.size(); iPlayerChairIndex++)
	{
		BYTE cbTempPlayerChairID = vecPlayerChairID[iPlayerChairIndex];
		BYTE cbTempRobotCardType = m_gameLogic.GetCardType(cbTempHandCard[cbTempPlayerChairID], NIUNIU_CARD_NUM);
		if (cbTempRobotCardType < CT_SPECIAL_MAX_TYPE)
		{
			cbPlayerCardType[cbTempPlayerChairID] = cbTempRobotCardType;
		}
	}
	bool bIsAllPlayerCardTypeIsNotNiu = true;
	std::string strAllPlayerCardType;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (cbPlayerCardType[iPlayerIndex] > CT_POINT)
		{
			bIsAllPlayerCardTypeIsNotNiu = false;
			strAllPlayerCardType += CStringUtility::FormatToString("%d ", cbPlayerCardType[iPlayerIndex]);

			//break;
		}
	}

	if (bIsAllPlayerCardTypeIsNotNiu == true)
	{
		// 所有真实玩家都已经无牛
		LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecPlayerChairID.size:%d,strAllPlayerCardType:%s",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecPlayerChairID.size(), strAllPlayerCardType.c_str());
		return false;
	}

	//获取剩余扑克
	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(cbTempHandCard, vecRemainCardData);
	//BYTE cbBrankerCardType = m_gameLogic.GetCardType(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);

	vector<BYTE> vecSpecialTypeCardData[GAME_PLAYER][CT_SPECIAL_MAX_TYPE];
	BYTE cbLoopHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbLoopHandCard, cbTempHandCard, sizeof(cbLoopHandCard));
	for (uint32 iPlayerChairIndex = 0; iPlayerChairIndex < vecPlayerChairID.size(); iPlayerChairIndex++)
	{
		BYTE cbTempPlayerChairID = vecPlayerChairID[iPlayerChairIndex];
		for (uint32 iCardIndex = 0; iCardIndex < vecRemainCardData.size(); iCardIndex++)
		{
			cbLoopHandCard[cbTempPlayerChairID][4] = vecRemainCardData[iCardIndex];
			BYTE cbTempCardType = m_gameLogic.GetCardType(cbLoopHandCard[cbTempPlayerChairID], NIUNIU_CARD_NUM);
			if (cbTempCardType<CT_SPECIAL_MAX_TYPE)
			{
				vecSpecialTypeCardData[cbTempPlayerChairID][cbTempCardType].push_back(vecRemainCardData[iCardIndex]);
			}
		}
	}
	vector<std::pair<uint32, BYTE>> vecSubPlayerCardType;
	// 每个真实玩家降一级牌型
	bool bIsSubSuccessPlayerCardType = false;
	std::string strPlayerSubInfo;
	for (uint32 iPlayerChairIndex = 0; iPlayerChairIndex < vecPlayerChairID.size(); iPlayerChairIndex++)
	{
		BYTE cbTempPlayerChairID = vecPlayerChairID[iPlayerChairIndex];
		strPlayerSubInfo += CStringUtility::FormatToString("chairid_%d uid_%d ", cbTempPlayerChairID, GetPlayerID(cbTempPlayerChairID));
		if (cbPlayerCardType[cbTempPlayerChairID] > CT_POINT)
		{
			BYTE cbTempPlayerCardType = cbPlayerCardType[cbTempPlayerChairID]-1;

			strPlayerSubInfo += CStringUtility::FormatToString("tempType_%d ", cbTempPlayerCardType);

			for (BYTE cbCardTypeIndex = cbPlayerCardType[cbTempPlayerChairID]-1; cbCardTypeIndex >= CT_POINT; cbCardTypeIndex--)
			{
				uint32 uSpecialTypeSize = vecSpecialTypeCardData[cbTempPlayerChairID][cbTempPlayerCardType].size();
				strPlayerSubInfo += CStringUtility::FormatToString("ssize_%d ", uSpecialTypeSize);
				if (uSpecialTypeSize > 0)
				{
					int iRealCardTypeIndex = g_RandGen.RandRange(0, uSpecialTypeSize - 1);
					BYTE cbRealCardData = vecSpecialTypeCardData[cbTempPlayerChairID][cbTempPlayerCardType][iRealCardTypeIndex];

					strPlayerSubInfo += CStringUtility::FormatToString("ctindex_%d data_0x%02X", iRealCardTypeIndex, cbRealCardData);

					m_cbHandCardData[cbTempPlayerChairID][4] = cbRealCardData;
					vecSubPlayerCardType.emplace_back(cbTempPlayerChairID, cbTempPlayerCardType);
					//if (cbTempPlayerCardType > CT_POINT)
					//{
					//	cbTempPlayerCardType--;
					//}
					bIsSubSuccessPlayerCardType = true;
					break;
				}
				else
				{
					if (cbTempPlayerCardType > CT_POINT)
					{
						cbTempPlayerCardType--;
					}
					strPlayerSubInfo += CStringUtility::FormatToString("tempType-_%d ", cbTempPlayerCardType);
				}
			}
		}
	}
	std::string strSubPlayerCardType;
	for (const auto & [cbChairID, cbCardType] : vecSubPlayerCardType)
	{
		strSubPlayerCardType += CStringUtility::FormatToString("chairid_%d,cardtype_%d ", cbChairID, cbCardType);
	}
	
	LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,vecPlayerChairID.size:%d,Success:%d,cbPlayerCardType:%s,strSubPlayerCardType:%s,strPlayerSubInfo:%s",
		GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), vecPlayerChairID.size(), bIsSubSuccessPlayerCardType, strAllPlayerCardType.c_str(), strSubPlayerCardType.c_str(), strPlayerSubInfo.c_str());
	
	return bIsSubSuccessPlayerCardType;
}

// 如果真实玩家当庄，降真实玩家牌型
bool CGameNiuniuTable::OnPlayerBrankerSubPlayerCardType()
{
	if (m_wBankerUser >= GAME_PLAYER)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d", GetRoomID(), GetTableID(), m_wBankerUser);
		return false;
	}

	BYTE cbTempHandCard[GAME_PLAYER][NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbTempHandCard, m_cbHandCardData, sizeof(cbTempHandCard));
	BYTE cbBrankerHandCardData[NIUNIU_CARD_NUM] = { 0 };
	memcpy(cbBrankerHandCardData, cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);

	BYTE cbBrankerCardType = m_gameLogic.GetCardType(cbTempHandCard[m_wBankerUser], NIUNIU_CARD_NUM);
	vector<BYTE> vecSubPlayerTypeCardData[CT_SPECIAL_MAX_TYPE];

	if (cbBrankerCardType <= CT_POINT)
	{
		LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,cbBrankerCardType:%d",
			GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), cbBrankerCardType);

		return false;
	}

	//获取剩余
	bool bIsHaveRemainCardType = false;
	vector<BYTE> vecRemainCardData;
	m_gameLogic.GetSubDataCard(cbTempHandCard, vecRemainCardData);
	for (uint32 iCardIndex = 0; iCardIndex < vecRemainCardData.size(); iCardIndex++)
	{
		cbBrankerHandCardData[4] = vecRemainCardData[iCardIndex];
		BYTE cbTempCardType = m_gameLogic.GetCardType(cbBrankerHandCardData, NIUNIU_CARD_NUM);
		if (cbTempCardType<CT_SPECIAL_MAX_TYPE)
		{
			vecSubPlayerTypeCardData[cbTempCardType].push_back(vecRemainCardData[iCardIndex]);
			bIsHaveRemainCardType = true;
		}
	}
	bool bIsSubPlayerCardType = false;
	BYTE cbTempBrankerCardType = cbBrankerCardType - 1;
	if (bIsHaveRemainCardType)
	{
		for (BYTE cbCardTypeIndex = cbTempBrankerCardType; cbCardTypeIndex >= CT_POINT; cbCardTypeIndex--)
		{
			if (vecSubPlayerTypeCardData[cbTempBrankerCardType].size() > 0)
			{
				int iRealCardTypeIndex = g_RandGen.RandRange(0, vecSubPlayerTypeCardData[cbTempBrankerCardType].size() - 1);
				BYTE cbRealCardData = vecSubPlayerTypeCardData[cbTempBrankerCardType][iRealCardTypeIndex];
				m_cbHandCardData[m_wBankerUser][4] = cbRealCardData;
				bIsSubPlayerCardType = true;
				break;
			}
			else
			{
				if (cbTempBrankerCardType > CT_POINT)
				{
					cbTempBrankerCardType--;
				}
			}
		}
	}

	LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,cbBrankerCardType:%d,cbTempBrankerCardType:%d,bIsHaveRemainCardType:%d,bIsSubPlayerCardType:%d,",
		GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), cbBrankerCardType, cbTempBrankerCardType, bIsHaveRemainCardType, bIsSubPlayerCardType);

	return bIsSubPlayerCardType;
}

bool CGameNiuniuTable::ProCtrlRobotWinScore(uint32 robotWinPro)
{
	bool bIsAllPlayerIn = true;
	bool bIsAllRobotIn = true;
	for (int iPlayerIndex = 0; iPlayerIndex < GAME_PLAYER; ++iPlayerIndex)
	{
		if (m_cbPlayStatus[iPlayerIndex] == FALSE)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(iPlayerIndex);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot())
		{
			bIsAllPlayerIn = false;
		}
		else
		{
			bIsAllRobotIn = false;
		}
	}

	bool bIsRobotCanWin = g_RandGen.RandRatio(robotWinPro, PRO_DENO_10000);
	bool bRobotIsRranker = false;
	CGamePlayer * pBrankerPlayer = GetPlayer(m_wBankerUser);
	if (pBrankerPlayer != NULL)
	{
		bRobotIsRranker = pBrankerPlayer->IsRobot();
	}
	bool bIsRobotWinScore = false;
	bool bIsSubPlayerCardType = false;
	int iLoopIndex = 0;
	if (bIsRobotCanWin && bIsAllRobotIn == false && bIsAllPlayerIn == false)
	{
		if (bRobotIsRranker)
		{
			bIsRobotWinScore = SetRobotBrankerCanWinScore();
			if (bIsRobotWinScore == false)
			{
				while (true)
				{
					if (bIsRobotWinScore == false)
					{
						bIsSubPlayerCardType = OnRobotBrankerSubPlayerCardType();
						bIsRobotWinScore = SetRobotBrankerCanWinScore();
					}
					if (bIsRobotWinScore)
					{
						break;
					}
					iLoopIndex++;
					if (iLoopIndex >= MAX_RAND_LOOP_COUNT)
					{
						break;
					}
				}
			}
		}
		else
		{
			bIsRobotWinScore = SetPlayerBrankerLostScore();
			if (bIsRobotWinScore == false)
			{
				while (true)
				{
					if (bIsRobotWinScore == false)
					{
						bIsSubPlayerCardType = OnPlayerBrankerSubPlayerCardType();
						bIsRobotWinScore = SetPlayerBrankerLostScore();
						if (bIsRobotWinScore == true)
						{
							break;
						}
					}
					iLoopIndex++;
					if (iLoopIndex >= MAX_RAND_LOOP_COUNT)
					{
						break;
					}
				}
			}
		}
	}
	LOG_DEBUG("roomid:%d,tableid:%d,m_wBankerUser:%d,uid:%d,robotWinPro:%d,bIsRobotCanWin:%d,bIsAllRobotIn:%d,bIsAllPlayerIn:%d,bRobotIsRranker:%d,iLoopIndex:%d,bIsSubPlayerCardType:%d,bIsRobotWinScore:%d",
		GetRoomID(), GetTableID(), m_wBankerUser, GetPlayerID(m_wBankerUser), robotWinPro, bIsRobotCanWin, bIsAllRobotIn, bIsAllPlayerIn, bRobotIsRranker, iLoopIndex, bIsSubPlayerCardType, bIsRobotWinScore);

	return bIsRobotWinScore;
}

bool CGameNiuniuTable::NoviceWelfareCtrlWinScore()
{
	uint32 noviceuid = 0;
	uint32 posrmb = 0;
	struct tagUserNewPlayerWelfareValue tagValue;
	CGamePlayer * pPlayer = HaveWelfareNovicePlayer();
	if (pPlayer != NULL)
	{
		noviceuid = pPlayer->GetUID();
		posrmb = pPlayer->GetPosRmb();
		tagValue = pPlayer->GetWelfareValue();
	}
	int isnewnowe = 0;
	int64 newmaxjetton = 0;
	int64 newsmaxwin = 0;
	if (m_pHostRoom != NULL)
	{
		isnewnowe = m_pHostRoom->GetNoviceWelfareOwe();
		tagRangeWelfare tempTagRange = m_pHostRoom->GetRangeWelfareByPosRmb(noviceuid, posrmb);
		newmaxjetton = tempTagRange.smaxjetton;
		newsmaxwin = tempTagRange.smaxwin;
	}

	bool bIsNoviceWelfareCtrl = false;
	int64 lNoviceWinScore = 0;
	bool bIsOverJetton = HaveOverWelfareMaxJettonScore(newmaxjetton);
	tagNewPlayerWelfareValue NewPlayerWelfareValue;
	bool bIsHitWelfarePro = false;


	uint32 real_welfarepro = 0;
	int fUseHitWelfare = 1;
	if (isnewnowe == 1 && bIsOverJetton == false && pPlayer != NULL && noviceuid != 0)
	{
		NewPlayerWelfareValue = CDataCfgMgr::Instance().GetNewPlayerWelfareValue(noviceuid, posrmb);
		struct tagUserNewPlayerWelfareValue & tagTempValue = pPlayer->GetWelfareValue();

		fUseHitWelfare = tagTempValue.frontIsHitWelfare;
		if (fUseHitWelfare == 0 && tagTempValue.jettonCount > 0)
		{
			real_welfarepro = NewPlayerWelfareValue.welfarepro + (tagTempValue.jettonCount * NewPlayerWelfareValue.lift_odds);
		}
		else
		{
			real_welfarepro = NewPlayerWelfareValue.welfarepro;
		}

		if (real_welfarepro > PRO_DENO_10000)
		{
			real_welfarepro = PRO_DENO_10000;
		}
		bIsHitWelfarePro = g_RandGen.RandRatio(real_welfarepro, PRO_DENO_10000);

		if (bIsHitWelfarePro)
		{
			SetControlPalyerWin(noviceuid);
			lNoviceWinScore = GetNoviceWinScore(noviceuid);
			if (lNoviceWinScore <= newsmaxwin)
			{
				bIsNoviceWelfareCtrl = true;
				SetChessWelfare(1);
			}
			else
			{
				//SetControlPalyerLost(noviceuid);
				bIsNoviceWelfareCtrl = false;
			}
		}
		else
		{
			//SetControlPalyerLost(noviceuid);
			bIsNoviceWelfareCtrl = false;
		}

		if (bIsNoviceWelfareCtrl)
		{
			tagTempValue.frontIsHitWelfare = 1;
			tagTempValue.jettonCount = 0;
		}
		else
		{
			tagTempValue.frontIsHitWelfare = 0;
			tagTempValue.jettonCount++;
		}
	}	
	LOG_DEBUG("dos_wel_ctrl - roomid:%d,tableid:%d,isnewnowe:%d, newmaxjetton:%lld, newsmaxwin:%lld,lNoviceWinScore:%lld, IsNoviceWelfareCtrl:%d, noviceuid:%d,posrmb:%d, bIsOverJetton:%d, ChessWelfare:%d,welfarepro:%d,real_welfarepro:%d,lift_odds:%d,fUseHitWelfare:%d,frontIsHitWelfare:%d,jettonCount:%d,bIsHitWelfarePro:%d",
		GetRoomID(), GetTableID(), isnewnowe, newmaxjetton, newsmaxwin, lNoviceWinScore, bIsNoviceWelfareCtrl, noviceuid, posrmb, bIsOverJetton, GetChessWelfare(), NewPlayerWelfareValue.welfarepro, real_welfarepro, NewPlayerWelfareValue.lift_odds, fUseHitWelfare, tagValue.frontIsHitWelfare, tagValue.jettonCount, bIsHitWelfarePro);
	return bIsNoviceWelfareCtrl;
}

void CGameNiuniuTable::SetCardDataControl()
{
	if (m_bIsMasterUserOper == false)
	{
		m_bIsProgressControlPalyer = ProgressControlPalyer();
	}

	// 幸运值福利
	bool bIsLuckyCtrl = false;
	if (!m_bIsMasterUserOper && !m_bIsProgressControlPalyer)
	{
		bIsLuckyCtrl = SetLuckyCtrl();
	}

	// 新用户福利
	if (!m_bIsMasterUserOper && !m_bIsProgressControlPalyer && !bIsLuckyCtrl)
	{
		m_bIsNoviceWelfareCtrl = NoviceWelfareCtrlWinScore();
	}

    // 活跃福利
    bool bIsAWControl = false;
    if (!m_bIsMasterUserOper && !m_bIsProgressControlPalyer && !bIsLuckyCtrl && !m_bIsNoviceWelfareCtrl)
    {
        bIsAWControl = ActiveWelfareCtrl();
    }

	bool bIsRobotWinScore = false;
	bool bIsRobotCanWin = g_RandGen.RandRatio(m_robotWinPro, PRO_DENO_10000);
	if (!m_bIsMasterUserOper && !m_bIsProgressControlPalyer && !bIsLuckyCtrl && !m_bIsNoviceWelfareCtrl && !bIsAWControl && bIsRobotCanWin)
	{
		bIsRobotWinScore = SetRobotWinCard();
		//bIsRobotWinScore = ProCtrlRobotWinScore(m_robotWinPro);
	}

	// add by har
	m_isNeedCheckStock = false;
	if (!m_bIsMasterUserOper && !m_bIsProgressControlPalyer && !bIsLuckyCtrl && !m_bIsNoviceWelfareCtrl && !bIsAWControl && !bIsRobotCanWin)
		m_isNeedCheckStock = true; // add by har end

	LOG_DEBUG("roomid:%d,tableid:%d,m_bIsMasterUserOper:%d,ControlPalyer:%d,bIsLuckyCtrl:%d,NoviceWelfareCtrl:%d,bIsAWControl:%d",
		GetRoomID(), GetTableID(), m_bIsMasterUserOper, m_bIsProgressControlPalyer, bIsLuckyCtrl, m_bIsNoviceWelfareCtrl, bIsAWControl);
	return;			
}

bool	CGameNiuniuTable::SetPlayerWinScore()
{
	//最大牌是谁
	if (GetOnlinePlayerNum() == 0) return false;
	CGamePlayer * pBrankerGamePlayer = GetPlayer(m_wBankerUser);
	if (pBrankerGamePlayer == NULL) return false;
	if (pBrankerGamePlayer->IsRobot())
	{
		uint16 minChairID = INVALID_CHAIR;
		uint8  multiple = 0;
		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE)
				continue;
			if (minChairID == INVALID_CHAIR) {
				minChairID = i;
				continue;
			}
			bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[minChairID], NIUNIU_CARD_NUM, multiple) != 1 ? false : true;
			if (bWin) {
				minChairID = i;
			}
		}
		if (minChairID == INVALID_CHAIR) {
			//LOG_DEBUG("最大牌座位id不存在");
			return true;
		}

		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == minChairID)
				continue;
			CGamePlayer* pTmp = GetPlayer(i);
			if (pTmp != NULL && pTmp->GetUID() == pBrankerGamePlayer->GetUID()) {
				uint8 tmp[NIUNIU_CARD_NUM];
				memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[i], m_cbHandCardData[minChairID], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[minChairID], tmp, NIUNIU_CARD_NUM);
				//LOG_DEBUG("玩家换牌成功:robot:%d--minChairID:%d", i, minChairID);
				return true;
			}
		}
		//LOG_DEBUG("未找到换牌对象:%d", minChairID);
	}
	else
	{
		uint16 maxChairID = INVALID_CHAIR;
		uint8  multiple = 0;
		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE)
				continue;
			if (maxChairID == INVALID_CHAIR) {
				maxChairID = i;
				continue;
			}
			bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM, multiple) == 1 ? false : true;
			if (bWin) {
				maxChairID = i;
			}
		}
		if (maxChairID == INVALID_CHAIR) {
			//LOG_DEBUG("最大牌座位id不存在");
			return true;
		}

		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == maxChairID)
				continue;
			CGamePlayer* pTmp = GetPlayer(i);
			if (pTmp != NULL && pTmp->GetUID()== pBrankerGamePlayer->GetUID()) {
				uint8 tmp[NIUNIU_CARD_NUM];
				memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[i], m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[maxChairID], tmp, NIUNIU_CARD_NUM);
				//LOG_DEBUG("玩家换牌成功:robot:%d--maxchairID:%d", i, maxChairID);
				return true;
			}
		}
		//LOG_DEBUG("未找到换牌对象:%d", maxChairID);
	}

	return true;
}

bool	CGameNiuniuTable::SetPlayerLostScore()
{
	//最大牌是谁
	if (GetOnlinePlayerNum() == 0) return false;
	CGamePlayer * pBrankerGamePlayer = GetPlayer(m_wBankerUser);
	if (pBrankerGamePlayer == NULL) return false;
	if (!pBrankerGamePlayer->IsRobot())
	{
		uint16 minChairID = INVALID_CHAIR;
		uint8  multiple = 0;
		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE)
				continue;
			if (minChairID == INVALID_CHAIR) {
				minChairID = i;
				continue;
			}
			bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[minChairID], NIUNIU_CARD_NUM, multiple) != 1 ? false : true;
			if (bWin) {
				minChairID = i;
			}
		}
		if (minChairID == INVALID_CHAIR) {
			//LOG_DEBUG("最大牌座位id不存在");
			return false;
		}

		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == minChairID)
				continue;
			CGamePlayer* pTmp = GetPlayer(i);
			if (pTmp != NULL && pTmp->GetUID() == pBrankerGamePlayer->GetUID()) {
				uint8 tmp[NIUNIU_CARD_NUM];
				memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[i], m_cbHandCardData[minChairID], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[minChairID], tmp, NIUNIU_CARD_NUM);
				//LOG_DEBUG("玩家换牌成功:robot:%d--minChairID:%d", i, minChairID);
				return true;
			}
		}
		//LOG_DEBUG("未找到换牌对象:%d", minChairID);
	}
	else
	{
		uint16 maxChairID = INVALID_CHAIR;
		uint8  multiple = 0;
		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE)
				continue;
			if (maxChairID == INVALID_CHAIR) {
				maxChairID = i;
				continue;
			}
			bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM, multiple) == 1 ? false : true;
			if (bWin) {
				maxChairID = i;
			}
		}
		if (maxChairID == INVALID_CHAIR) {
			//LOG_DEBUG("最大牌座位id不存在");
			return true;
		}

		for (uint16 i = 0; i<GAME_PLAYER; ++i)
		{
			if (m_cbPlayStatus[i] == FALSE || i == maxChairID)
				continue;
			CGamePlayer* pTmp = GetPlayer(i);
			if (pTmp != NULL && pTmp->GetUID() == pBrankerGamePlayer->GetUID()) {
				uint8 tmp[NIUNIU_CARD_NUM];
				memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[i], m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM);
				memcpy(m_cbHandCardData[maxChairID], tmp, NIUNIU_CARD_NUM);
				//LOG_DEBUG("玩家换牌成功:robot:%d--maxchairID:%d", i, maxChairID);
				return true;
			}
		}
		//LOG_DEBUG("未找到换牌对象:%d", maxChairID);
	}

	return true;
}

bool    CGameNiuniuTable::SetControlPalyerWin(uint32 control_uid)
{
	uint16 maxChairID = INVALID_CHAIR;
	uint8  multiple = 0;
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		if (maxChairID == INVALID_CHAIR)
		{
			maxChairID = i;
			continue;
		}
		bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM, multiple) == 1 ? false : true;
		if (bWin)
		{
			maxChairID = i;
		}
	}
	if (maxChairID == INVALID_CHAIR)
	{
		LOG_DEBUG("max card is error - roomid:%d,tableid:%d,maxChairID:%d,control_uid:%d", m_pHostRoom->GetRoomID(), GetTableID(), maxChairID, control_uid);

		return false;
	}
	CGamePlayer* pTar = GetPlayer(maxChairID);
	if (pTar != NULL && pTar->GetUID()== control_uid)
	{
		LOG_DEBUG("max card is - roomid:%d,tableid:%d,maxChairID:%d,control_uid:%d", m_pHostRoom->GetRoomID(), GetTableID(), maxChairID, control_uid);

		return true;
	}

	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == maxChairID)
		{
			continue;
		}
		CGamePlayer* pTmp = GetPlayer(i);
		if (pTmp != NULL && pTmp->GetUID()== control_uid)
		{
			uint8 tmp[NIUNIU_CARD_NUM];
			memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[i], m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[maxChairID], tmp, NIUNIU_CARD_NUM);
			LOG_DEBUG("changer card success - roomid:%d,tableid:%d,control_uid:%d,i%d,maxchairID:%d", GetRoomID(), GetTableID(), control_uid, i, maxChairID);
			return true;
		}
	}
	LOG_DEBUG("changer is no find - roomid:%d,tableid:%d,maxChairID:%d,control_uid:%d", m_pHostRoom->GetRoomID(), GetTableID(), maxChairID, control_uid);
	return false;
}

bool    CGameNiuniuTable::SetControlPalyerLost(uint32 control_uid)
{
	uint16 minChairID = INVALID_CHAIR;
	uint8  multiple = 0;
	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
			continue;
		if (minChairID == INVALID_CHAIR) {
			minChairID = i;
			continue;
		}
		bool bLost = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[minChairID], NIUNIU_CARD_NUM, multiple) == -1 ? false : true;
		if (bLost) {
			minChairID = i;
		}
	}
	if (minChairID == INVALID_CHAIR) {
		LOG_DEBUG("minChairID is no find - roomid:%d,tableid:%d,control_uid:%d", GetRoomID(), GetTableID(), control_uid);
		return false;
	}
	CGamePlayer* pTar = GetPlayer(minChairID);
	if (pTar != NULL && pTar->GetUID() == control_uid) {
		LOG_DEBUG("min card is - roomid:%d,tableid:%d,minChairID:%d,control_uid:%d", GetRoomID(), GetTableID(), minChairID, control_uid);
		return true;
	}

	for (uint16 i = 0; i<GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == minChairID)
			continue;
		CGamePlayer* pTmp = GetPlayer(i);
		if (pTmp != NULL && pTmp->GetUID() == control_uid)
		{
			uint8 tmp[NIUNIU_CARD_NUM];
			memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[i], m_cbHandCardData[minChairID], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[minChairID], tmp, NIUNIU_CARD_NUM);
			LOG_DEBUG("changer card success - roomid:%d,tableid:%d,control_uid:%d,i%d,minChairID:%d", m_pHostRoom->GetRoomID(), GetTableID(), control_uid, i, minChairID);
			return true;
		}
	}
	LOG_DEBUG("changer is no find - roomid:%d,tableid:%d,minChairID:%d", m_pHostRoom->GetRoomID(), GetTableID(), minChairID);
	return false;
}

//设置机器人赢金币
bool    CGameNiuniuTable::SetRobotWinCard()
{
    //最大牌是谁
	if (GetOnlinePlayerNum() == 0)
	{
		return true;
	}
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
		CGamePlayer * pPlayer = GetPlayer(i);
		if (pPlayer == NULL)
		{
			continue;
		}
		if (pPlayer->IsRobot() == true && i == m_wBankerUser)
		{
			SetControlPalyerWin(pPlayer->GetUID());
			return true;
		}
	}

    uint16 maxChairID = INVALID_CHAIR;
    uint8  multiple   = 0;
    for(uint16 i=0;i<GAME_PLAYER;++i)
    {
		if (m_cbPlayStatus[i] == FALSE)
		{
			continue;
		}
        if(maxChairID == INVALID_CHAIR)
		{
            maxChairID = i;
            continue;
        }
        bool bWin = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM,m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM,multiple) == 1 ? false : true;
        if(bWin)
		{
            maxChairID = i;
        }
    }
    if(maxChairID == INVALID_CHAIR)
	{
        LOG_DEBUG("error - roomid:%d,tableid:%d,chessid:%s,Banker:%d,uid:%d",
			GetRoomID(), GetTableID(), GetChessID().c_str(), m_wBankerUser, GetPlayerID(m_wBankerUser));
        return false;
    }
    CGamePlayer* pTar = GetPlayer(maxChairID);
    if(pTar != NULL && pTar->IsRobot())
	{
		LOG_DEBUG("roomid:%d,tableid:%d,chessid:%s,Banker:%d,buid:%d,rchairID:%d,ruid:%d",
			GetRoomID(), GetTableID(), GetChessID().c_str(), m_wBankerUser, GetPlayerID(m_wBankerUser), maxChairID, GetPlayerID(maxChairID));
        return true;
    }

	for(uint16 i=0;i<GAME_PLAYER;++i)
    {
		if (m_cbPlayStatus[i] == FALSE || i == maxChairID)
		{
			continue;
		}
        CGamePlayer* pTmp = GetPlayer(i);
        if(pTmp != NULL && pTmp->IsRobot())
		{
            uint8 tmp[NIUNIU_CARD_NUM];
            memcpy(tmp,m_cbHandCardData[i], NIUNIU_CARD_NUM);
            memcpy(m_cbHandCardData[i],m_cbHandCardData[maxChairID], NIUNIU_CARD_NUM);
            memcpy(m_cbHandCardData[maxChairID],tmp,5);
            //LOG_DEBUG("玩家换牌成功:robot:%d--maxchairID:%d",i,maxChairID);

			LOG_DEBUG("roomid:%d,tableid:%d,chessid:%s,Banker:%d,buid:%d,rchairID:%d,ruid:%d",
				GetRoomID(), GetTableID(), GetChessID().c_str(), m_wBankerUser, GetPlayerID(m_wBankerUser), maxChairID, GetPlayerID(maxChairID));

            return true;
        }
    }
	LOG_DEBUG("error - roomid:%d,tableid:%d,chessid:%s,Banker:%d,uid:%d",
		GetRoomID(), GetTableID(), GetChessID().c_str(), m_wBankerUser, GetPlayerID(m_wBankerUser));
	return false;   
}

bool CGameNiuniuTable::ActiveWelfareCtrl()
{
    LOG_DEBUG("enter ActiveWelfareCtrl ctrl player count:%d.", m_aw_ctrl_player_list.size());

    //设置当前桌子游戏玩家列表
    for (uint16 i = 0; i < GAME_PLAYER; ++i)
    {
        if (m_cbPlayStatus[i] == FALSE)
        {
            continue;
        }
        else
        {
            CGamePlayer* pTmp = GetPlayer(i);
            if (pTmp != NULL && !pTmp->IsRobot())
            {
                m_curr_bet_user.insert(pTmp->GetUID());
            }
        }
    }

    //获取当前局活跃福利的控制玩家列表
    GetActiveWelfareCtrlPlayerList();

    vector<tagAWPlayerInfo>::iterator iter = m_aw_ctrl_player_list.begin();
    for (; iter != m_aw_ctrl_player_list.end(); iter++)
    {
        uint32 control_uid = iter->uid;

		//判断当前控制玩家是否在配置概率范围内
		uint32 tmp = rand() % 100;
		uint32 probability = iter->probability;
		if (tmp > probability)
		{
			LOG_DEBUG("The current player is not in config rate - control_uid:%d tmp:%d probability:%d", control_uid, tmp, probability)
				continue;
		}

		LOG_DEBUG("The current player in config rate - control_uid:%d tmp:%d probability:%d", control_uid, tmp, probability)

        if (SetControlPalyerWinForAW(control_uid, iter->max_win))
        {
            LOG_DEBUG("search success current player - uid:%d max_win:%d", control_uid, iter->max_win);
            m_aw_ctrl_uid = control_uid;   //设置当前活跃福利所控的玩家ID
            return true;
        }
        else
        {
            continue;
        }
    }
    LOG_DEBUG("the all ActiveWelfareCtrl player is search fail. return false.");
    return false;
}

bool CGameNiuniuTable::SetControlPalyerWinForAW(uint32 control_uid, int64 max_win)
{
    int irount_count = 1000;
    int iRountIndex = 0;

    int64 lAwWinScore = 0;

    for (; iRountIndex < irount_count; iRountIndex++)
    {
        lAwWinScore = GetNoviceWinScore(control_uid);
        if (lAwWinScore > 0 && lAwWinScore <= max_win)
        {
            LOG_DEBUG("search find success. roomid:%d,tableid:%d,control_uid:%d,max_win:%lld,lAwWinScore:%lld", m_pHostRoom->GetRoomID(), GetTableID(), control_uid, max_win, lAwWinScore);
            return true;
        }
        else
        {
            m_gameLogic.RandCardList(m_cbHandCardData[0], sizeof(m_cbHandCardData) / sizeof(m_cbHandCardData[0][0]));
        }
    }

    LOG_DEBUG("search find success is fail. roomid:%d,tableid:%d,control_uid:%d,max_win:%lld", m_pHostRoom->GetRoomID(), GetTableID(), control_uid, max_win);
    return false;
}

bool CGameNiuniuTable::SetLuckyCtrl()
{
	uint32 win_uid = 0;
	set<uint32> set_lose_uid;
	set_lose_uid.clear();

	bool flag = GetTableLuckyFlag(win_uid, set_lose_uid);

	LOG_DEBUG("roomid:%d tableid:%d flag:%d win_uid:%d set_lose_uid size:%d.", m_pHostRoom->GetRoomID(), GetTableID(), flag, win_uid, set_lose_uid.size());

	if (!flag)
	{
		m_lucky_flag = false;
		return false;
	}

	m_set_ctrl_lucky_uid.clear();
	m_lucky_flag = true;

	//设置当前赢家手牌
	uint8 chairid = 0;
	bool bIsCtrl = false;

	if (win_uid != 0)
	{
		CGamePlayer * pGamePlayer = GetGamePlayerByUid(win_uid);
		if (pGamePlayer != NULL)
		{
			chairid = GetChairID(pGamePlayer);
			if (chairid != INVALID_CHAIR && m_cbPlayStatus[chairid] == TRUE)
			{
				bIsCtrl = SetControlPalyerWin(win_uid);
				if (bIsCtrl)
				{
					CGamePlayer * pGamePlayer = GetGamePlayerByUid(win_uid);
					if (pGamePlayer != NULL)
					{
						chairid = GetChairID(pGamePlayer);
						if (chairid != INVALID_CHAIR)
						{
							LOG_DEBUG("the lucky win player. uid:%d chairid:%d HandCardData:0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
								win_uid, chairid, m_cbHandCardData[chairid][0], m_cbHandCardData[chairid][1], m_cbHandCardData[chairid][2], m_cbHandCardData[chairid][3], m_cbHandCardData[chairid][4]);
						}
					}
					m_set_ctrl_lucky_uid.insert(win_uid);
					LOG_DEBUG("the set lucky win player is success uid:%d.", win_uid);
				}
				else
				{
					LOG_DEBUG("the set lucky win player is fail uid:%d.", win_uid);
				}
			}
			else
			{
				LOG_DEBUG("the chair is error or chair player is not game. uid:%d chair:%d.", win_uid, chairid);
			}
		}
		else
		{
			LOG_DEBUG("get player info is fail.uid:%d", win_uid);
		}
	}

	//设置当前输家手牌
	uint32 lose_uid = 0;
	for (uint32 uid : set_lose_uid)
	{
		lose_uid = uid;
		CGamePlayer * pGamePlayer = GetGamePlayerByUid(lose_uid);
		if (pGamePlayer != NULL)
		{
			chairid = GetChairID(pGamePlayer);
			if (chairid != INVALID_CHAIR && m_cbPlayStatus[chairid] == TRUE)
			{
				bIsCtrl = SetLostForLuckyCtrl(lose_uid);
				if (bIsCtrl)
				{
					CGamePlayer * pGamePlayer = GetGamePlayerByUid(lose_uid);
					if (pGamePlayer != NULL)
					{
						chairid = GetChairID(pGamePlayer);
						if (chairid != INVALID_CHAIR)
						{
							LOG_DEBUG("the lucky lose player. uid:%d chairid:%d HandCardData:0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
								lose_uid, chairid, m_cbHandCardData[chairid][0], m_cbHandCardData[chairid][1], m_cbHandCardData[chairid][2], m_cbHandCardData[chairid][3], m_cbHandCardData[chairid][4]);
						}
					}
					m_set_ctrl_lucky_uid.insert(lose_uid);
					LOG_DEBUG("the set lucky lose player is success uid:%d.", lose_uid);
				}
				else
				{
					LOG_DEBUG("the set lucky lose player is fail uid:%d.", lose_uid);
				}
			}
			else
			{
				LOG_DEBUG("the chair is error or chair player is not game. uid:%d chair:%d.", lose_uid, chairid);
			}
		}
		else
		{
			LOG_DEBUG("get player info is fail.uid:%d", lose_uid);
		}
	}
	LOG_DEBUG("the set lucky success. roomid:%d tid:%d.", m_pHostRoom->GetRoomID(), GetTableID());
	return true;
}

bool    CGameNiuniuTable::SetLostForLuckyCtrl(uint32 control_uid)
{
	uint16 minChairID = INVALID_CHAIR;
	uint8  multiple = 0;
	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE)
			continue;
		if (minChairID == INVALID_CHAIR) 
		{
			minChairID = i;
			continue;
		}

		//过滤掉已经换牌的玩家
		CGamePlayer * pPlayer = GetPlayer(i);
		if (!pPlayer)
		{
			auto iter = m_set_ctrl_lucky_uid.find(pPlayer->GetUID());
			if (iter != m_set_ctrl_lucky_uid.end())
			{
				continue;
			}
		}

		bool bLost = m_gameLogic.CompareCard(m_cbHandCardData[i], NIUNIU_CARD_NUM, m_cbHandCardData[minChairID], NIUNIU_CARD_NUM, multiple) == -1 ? false : true;
		if (bLost) {
			minChairID = i;
		}
	}
	if (minChairID == INVALID_CHAIR) {
		LOG_DEBUG("minChairID is no find - roomid:%d,tableid:%d,control_uid:%d", GetRoomID(), GetTableID(), control_uid);
		return false;
	}
	CGamePlayer* pTar = GetPlayer(minChairID);
	if (pTar != NULL && pTar->GetUID() == control_uid) {
		LOG_DEBUG("min card is - roomid:%d,tableid:%d,minChairID:%d,control_uid:%d", GetRoomID(), GetTableID(), minChairID, control_uid);
		return true;
	}

	for (uint16 i = 0; i < GAME_PLAYER; ++i)
	{
		if (m_cbPlayStatus[i] == FALSE || i == minChairID)
			continue;
		CGamePlayer* pTmp = GetPlayer(i);
		if (pTmp != NULL && pTmp->GetUID() == control_uid)
		{
			uint8 tmp[NIUNIU_CARD_NUM];
			memcpy(tmp, m_cbHandCardData[i], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[i], m_cbHandCardData[minChairID], NIUNIU_CARD_NUM);
			memcpy(m_cbHandCardData[minChairID], tmp, NIUNIU_CARD_NUM);
			LOG_DEBUG("changer card success - roomid:%d,tableid:%d,control_uid:%d,i%d,minChairID:%d", m_pHostRoom->GetRoomID(), GetTableID(), control_uid, i, minChairID);
			return true;
		}
	}
	LOG_DEBUG("changer is no find - roomid:%d,tableid:%d,minChairID:%d", m_pHostRoom->GetRoomID(), GetTableID(), minChairID);
	return false;
}

// 设置库存输赢  add by har
bool CGameNiuniuTable::SetStockWinLose() 
{
	int64 stockChange = m_pHostRoom->IsStockChangeCard(this);
	if (stockChange == 0)
	{
		LOG_DEBUG("stockChange is zero roomid:%d,tableid:%d",GetRoomID(), GetTableID());
		return false;
	}

	int64 szWinScore[GAME_PLAYER] = { 0 };
	int64 playerAllWinScore = 0;
	int i = 0;
	// 循环，直到找到满足条件的牌组合
	while (true) {
		playerAllWinScore = CalculateScore(szWinScore, false);
		if (CheckStockChange(stockChange, playerAllWinScore, i)) 
		{
			LOG_DEBUG("SetStockWinLose suc roomid:%d,tableid:%d,playerAllWinScore:%lld,stockChange:%lld,i:%d",
				GetRoomID(), GetTableID(), playerAllWinScore, stockChange, i);
			return true;
		}
		if (++i > 999)
		    break;
		ZeroMemory(szWinScore, sizeof(szWinScore)); // 这一步不能漏掉！
		//重新洗牌
		m_gameLogic.RandCardList(m_cbHandCardData[0], sizeof(m_cbHandCardData) / sizeof(m_cbHandCardData[0][0]));
	}

	LOG_ERROR("SetStockWinLose fail roomid:%d,tableid:%d,playerAllWinScore:%lld,stockChange:%lld",
		GetRoomID(), GetTableID(), playerAllWinScore, stockChange);
	return false;
}
