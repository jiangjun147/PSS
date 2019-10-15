// IObject.h
// 虚接口化所有DLL用到的类
// add by freeeyes
// 2008-04-06

#ifndef _IOBJECT_H
#define _IOBJECT_H

#include "IMessage.h"
#include "IMessageManager.h"
#include "ILogManager.h"
#include "IConnectManager.h"
#include "IPacketManager.h"
#include "IClientManager.h"
#include "IUDPConnectManager.h"
#include "ITimerManager.h"
#include "IModuleMessageManager.h"
#include "IControlListen.h"
#include "IModuleInfo.h"
#include "IServerManager.h"
#include "IMessageBlockManager.h"
#include "IFrameCommand.h"
#include "ITSTimer.h"
#include "ITTyClientManager.h"
#include "IMessageQueueManager.h"
#include "ITMService.h"
#include "IEchartlog.h"

class CServerObject
{
public:
    CServerObject()
    {
        m_pIMessageManager      = NULL;
        m_pLogManager           = NULL;
        m_pConnectManager       = NULL;
        m_pPacketManager        = NULL;
        m_pClientManager        = NULL;
        m_pUDPConnectManager    = NULL;
        m_pModuleMessageManager = NULL;
        m_pContorlListen        = NULL;
        m_pIServerManager       = NULL;
        m_pMessageBlockManager  = NULL;
        m_pTimerManager         = NULL;
        m_pIModuleInfo          = NULL;
        m_pFrameCommand         = NULL;
        m_pTMService            = NULL;
        m_pTTyClientManager     = NULL;
        m_pMessageQueueManager  = NULL;
        m_pEchartlog            = NULL;
    }

    virtual ~CServerObject() {}
    void SetMessageManager(IMessageManager* pIMessageManager)
    {
        m_pIMessageManager = pIMessageManager;
    }
    void SetLogManager(ILogManager* pLogManager)
    {
        m_pLogManager = pLogManager;
    }
    void SetConnectManager(IConnectManager* pConnectManager)
    {
        m_pConnectManager = pConnectManager;
    }
    void SetPacketManager(IPacketManager*  pPacketManager)
    {
        m_pPacketManager = pPacketManager;
    }
    void SetClientManager(IClientManager* pClientManager)
    {
        m_pClientManager = pClientManager;
    }
    void SetUDPConnectManager(IUDPConnectManager* pUDPConnectManager)
    {
        m_pUDPConnectManager = pUDPConnectManager;
    }
    void SetTimerManager(ActiveTimer* pTimerManager)
    {
        m_pTimerManager = pTimerManager;
    }
    void SetModuleMessageManager(IModuleMessageManager* pModuleMessageManager)
    {
        m_pModuleMessageManager = pModuleMessageManager;
    }
    void SetControlListen(IControlListen* pControlListen)
    {
        m_pContorlListen = pControlListen;
    }
    void SetModuleInfo(IModuleInfo* pModuleInfo)
    {
        m_pIModuleInfo = pModuleInfo;
    }
    void SetServerManager(IServerManager* pIServerManager)
    {
        m_pIServerManager = pIServerManager;
    }
    void SetMessageBlockManager(IMessageBlockManager* pMessageBlockManager)
    {
        m_pMessageBlockManager = pMessageBlockManager;
    }

    void SetFrameCommand(IFrameCommand* pFrameCommand)
    {
        m_pFrameCommand = pFrameCommand;
    }

    void SetTMSServce(ITMService* pTMService)
    {
        m_pTMService = pTMService;
    }

    void SetTTyClientManager(ITTyClientManager* pTTyClientManager)
    {
        m_pTTyClientManager = pTTyClientManager;
    }

    void SetMessageQueueManager(IMessageQueueManager* pMessageQueueManager)
    {
        m_pMessageQueueManager = pMessageQueueManager;
    }

    void SetEchartlog(IEchartlog* pEchartlog)
    {
        m_pEchartlog = pEchartlog;
    }

    IMessageManager*       GetMessageManager()
    {
        return m_pIMessageManager;
    }
    ILogManager*           GetLogManager()
    {
        return m_pLogManager;
    }
    IConnectManager*       GetConnectManager()
    {
        return m_pConnectManager;
    }
    IPacketManager*        GetPacketManager()
    {
        return m_pPacketManager;
    }
    IClientManager*        GetClientManager()
    {
        return m_pClientManager;
    }
    IUDPConnectManager*    GetUDPConnectManager()
    {
        return m_pUDPConnectManager;
    }
    IModuleMessageManager* GetModuleMessageManager()
    {
        return m_pModuleMessageManager;
    }
    IControlListen*        GetControlListen()
    {
        return m_pContorlListen;
    }
    IModuleInfo*           GetModuleInfo()
    {
        return m_pIModuleInfo;
    }
    IServerManager*        GetServerManager()
    {
        return m_pIServerManager;
    }
    IMessageBlockManager*  GetMessageBlockManager()
    {
        return m_pMessageBlockManager;
    }

    IFrameCommand* GetFrameCommand()
    {
        return m_pFrameCommand;
    }

    ITMService* GetTMService()
    {
        return m_pTMService;
    }

    ITTyClientManager* GetTTyClientManager()
    {
        return m_pTTyClientManager;
    }

    IMessageQueueManager* GetMessageQueueManager()
    {
        return m_pMessageQueueManager;
    }

    ActiveTimer* GetAceTimerManager()
    {
        return m_pTimerManager;
    }

    IEchartlog* GetEchartlog()
    {
        return m_pEchartlog;
    }

private:
    IMessageManager*       m_pIMessageManager;
    ILogManager*           m_pLogManager;
    IConnectManager*       m_pConnectManager;
    IPacketManager*        m_pPacketManager;
    IClientManager*        m_pClientManager;
    IUDPConnectManager*    m_pUDPConnectManager;
    ActiveTimer*           m_pTimerManager;
    IModuleMessageManager* m_pModuleMessageManager;
    IControlListen*        m_pContorlListen;
    IModuleInfo*           m_pIModuleInfo;
    IServerManager*        m_pIServerManager;
    IMessageBlockManager*  m_pMessageBlockManager;
    IFrameCommand*         m_pFrameCommand;
    ITMService*            m_pTMService;
    ITTyClientManager*     m_pTTyClientManager;
    IMessageQueueManager*  m_pMessageQueueManager;
    IEchartlog*            m_pEchartlog;
};

typedef ACE_Singleton<CServerObject, ACE_Null_Mutex> App_ServerObject;

#endif
