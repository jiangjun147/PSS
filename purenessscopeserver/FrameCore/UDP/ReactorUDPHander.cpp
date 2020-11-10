#include "ReactorUDPHander.h"

CReactorUDPHander::CReactorUDPHander(void)
{
}

CReactorUDPHander::~CReactorUDPHander(void)
{
	if (nullptr != m_pBlockMessage)
	{
		m_pBlockMessage->release();
	}

	if (nullptr != m_pBlockRecv)
	{
		m_pBlockRecv->release();
	}

	ACE_Reactor_Mask close_mask = ACE_Event_Handler::ALL_EVENTS_MASK | ACE_Event_Handler::DONT_CALL;
	this->reactor()->remove_handler(this, close_mask);
	m_skRemote.close();
}

int CReactorUDPHander::OpenAddress(const ACE_INET_Addr& AddrRemote, ACE_Reactor* pReactor)
{
    if (m_skRemote.open(AddrRemote) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::OpenAddress]Open error(%d).\n", errno));
        return -1;
    }

    reactor(pReactor);

	if (-1 == this->reactor()->register_handler(this, ACE_Event_Handler::READ_MASK))
	{
		OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::OpenAddress] Addr is register_handler error(%d).\n", errno));
		return -1;
	}

    return Init_Open_Address(AddrRemote);
}

void CReactorUDPHander::Close(uint32 u4ConnectID)
{
    App_UDPConnectIDManager::instance()->DeleteConnectID(u4ConnectID);
}

ACE_HANDLE CReactorUDPHander::get_handle(void) const
{
    return m_skRemote.get_handle();
}

int CReactorUDPHander::handle_input(ACE_HANDLE fd)
{
    if(fd == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::handle_input]fd is ACE_INVALID_HANDLE.\n"));
        return -1;
    }

    ACE_INET_Addr addrRemote;
    int nDataLen = (int)m_skRemote.recv(m_pBlockRecv->wr_ptr(), m_pBlockRecv->size(), addrRemote);

    if(nDataLen > 0)
    {
        //获得当前ConnectID
		bool blNew = false;
		uint32 u4ConnectID = App_UDPConnectIDManager::instance()->GetConnetID(addrRemote.get_host_addr(),
            addrRemote.get_port_number(),
			blNew);

		if (true == blNew)
		{
			//如果是新的链接，发送新链接建立事件
            Send_Hander_Event(u4ConnectID, PACKET_CONNECT, addrRemote);
		}

        //处理接收包的数据
        if (false == CheckMessage(u4ConnectID, m_pBlockRecv->rd_ptr(), (uint32)nDataLen, addrRemote))
        {
            OUR_DEBUG((LM_INFO, "[CReactorUDPHander::handle_input]CheckMessage fail.\n"));
        }

        m_pBlockRecv->reset();
    }

    return 0;
}

int CReactorUDPHander::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask close_mask)
{
    if(handle == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::handle_close]close_mask = %d.\n", (uint32)close_mask));
    }

    m_skRemote.close();
    return 0;
}

void CReactorUDPHander::SetPacketParseInfoID(uint32 u4PacketParseInfoID)
{
    m_u4PacketParseInfoID = u4PacketParseInfoID;
}

bool CReactorUDPHander::SendMessage(CSendMessageInfo objSendMessageInfo, uint32& u4PacketSize)
{
    ACE_Message_Block* pMbData = nullptr;

	_ClientIPInfo objClientIPInfo = App_UDPConnectIDManager::instance()->GetConnectIP(objSendMessageInfo.u4ConnectID);
	if (objClientIPInfo.m_u2Port == 0)
	{
		//没有找到要发送的端口，不在发送
		OUR_DEBUG((LM_INFO, "[CProactorUDPHandler::SendMessage]no find ConnectID=%d.\n", objSendMessageInfo.u4ConnectID));
		return false;
	}

	_Send_Message_Param obj_Send_Message_Param;
	obj_Send_Message_Param.m_u4PacketParseInfoID = m_u4PacketParseInfoID;
	obj_Send_Message_Param.m_blDlete             = objSendMessageInfo.blDelete;
	obj_Send_Message_Param.m_u2Port              = objClientIPInfo.m_u2Port;
	obj_Send_Message_Param.m_strClientIP         = objClientIPInfo.m_strClientIP;
	obj_Send_Message_Param.m_u2CommandID         = objSendMessageInfo.u2CommandID;
	obj_Send_Message_Param.m_emSendType          = objSendMessageInfo.emSendType;

    u4PacketSize = objSendMessageInfo.pBuffPacket->GetPacketLen();

	bool blState = Udp_Common_Send_Message(obj_Send_Message_Param,
		objSendMessageInfo.pBuffPacket,
		m_skRemote,
		m_pPacketParseInfo,
		m_pBlockMessage);

    if (false == blState)
    {
        //释放发送体
        App_MessageBlockManager::instance()->Close(pMbData);

        return false;
    }

	if (true == blState)
	{
        SaveSendInfo(objSendMessageInfo.pBuffPacket->GetPacketLen());
	}

	//回收IBuffPacket
	if (true == objSendMessageInfo.blDelete)
	{
		App_BuffPacketManager::instance()->Delete(objSendMessageInfo.pBuffPacket);
	}

    return true;
}

bool CReactorUDPHander::PutSendPacket(uint32 u4ConnectID, ACE_Message_Block* pMbData, uint32 u4Size, const ACE_Time_Value tvSend)
{
	//立即发送
	_ClientIPInfo objClientIPInfo = App_UDPConnectIDManager::instance()->GetConnectIP(u4ConnectID);
	if (objClientIPInfo.m_u2Port == 0)
	{
		//没有找到要发送的端口，不在发送
		OUR_DEBUG((LM_INFO, "[CProactorUDPHandler::PutSendPacket]no find ConnectID=%d.\n", u4ConnectID));
		return false;
	}

	ACE_INET_Addr AddrRemote;
	int nErr = AddrRemote.set(objClientIPInfo.m_u2Port, objClientIPInfo.m_strClientIP.c_str());

	if (nErr != 0)
	{
		OUR_DEBUG((LM_INFO, "[PutSendPacket]set_address error[%d].\n", errno));
		return false;
	}

	auto u4SendSize = (uint32)m_skRemote.send(pMbData->rd_ptr(), pMbData->length(), AddrRemote);

	if (u4SendSize != u4Size)
	{
		return false;
	}
	else
	{
        SaveSendInfo((uint32)u4SendSize);
        m_atvOutput = tvSend;
	}

	//发送完成回收
	App_MessageBlockManager::instance()->Close(pMbData);

	return true;
}

void CReactorUDPHander::SetIsLog(bool blIsLog)
{
    ACE_UNUSED_ARG(blIsLog);
}

_ClientConnectInfo CReactorUDPHander::GetClientConnectInfo() const
{
    _ClientConnectInfo ClientConnectInfo;
    ClientConnectInfo.m_blValid       = true;
    ClientConnectInfo.m_u4ConnectID   = 0;
    ClientConnectInfo.m_u4AliveTime   = 0;
    ClientConnectInfo.m_u4BeginTime   = (uint32)m_atvInput.sec();
    ClientConnectInfo.m_u4AllRecvSize = m_u4RecvSize;
    ClientConnectInfo.m_u4AllSendSize = m_u4SendSize;
    ClientConnectInfo.m_u4RecvCount   = m_u4RecvPacketCount;
    ClientConnectInfo.m_u4SendCount   = m_u4SendPacketCount;
    return ClientConnectInfo;
}

bool CReactorUDPHander::CheckMessage(uint32 u4ConnectID, const char* pData, uint32 u4Len, ACE_INET_Addr addrRemote)
{
    m_atvInput = ACE_OS::gettimeofday();

    if(m_pPacketParseInfo->m_u1PacketParseType == PACKET_WITHHEAD)
    {
        m_objPacketParse.SetPacket_Head_Src_Length(m_pPacketParseInfo->m_u4OrgLength);

        if(u4Len < m_objPacketParse.GetPacketHeadSrcLen())
        {
            return false;
        }

        //将完整的数据包转换为PacketParse对象
        ACE_Message_Block* pMBHead = App_MessageBlockManager::instance()->Create(m_objPacketParse.GetPacketHeadSrcLen());
        memcpy_safe(pData, m_objPacketParse.GetPacketHeadSrcLen(), pMBHead->wr_ptr(), m_objPacketParse.GetPacketHeadSrcLen());
        pMBHead->wr_ptr(m_objPacketParse.GetPacketHeadLen());

        bool blRet = Udp_Common_Recv_Head(u4ConnectID, pMBHead, &m_objPacketParse, m_pPacketParseInfo, u4Len);

        if (false == blRet)
        {
            App_MessageBlockManager::instance()->Close(pMBHead);
            return false;
        }

        //如果包含包体
        if(m_objPacketParse.GetPacketBodySrcLen() > 0)
        {
            const char* pBody = pData + m_objPacketParse.GetPacketHeadSrcLen();
            ACE_Message_Block* pMBBody = App_MessageBlockManager::instance()->Create(m_objPacketParse.GetPacketBodySrcLen());
            memcpy_safe(pBody, m_objPacketParse.GetPacketBodySrcLen(), pMBBody->wr_ptr(), m_objPacketParse.GetPacketBodySrcLen());
            pMBBody->wr_ptr(m_objPacketParse.GetPacketBodySrcLen());

            bool blStateBody = Udp_Common_Recv_Body(u4ConnectID, pMBBody, &m_objPacketParse, m_pPacketParseInfo);

            if (false == blStateBody)
            {
                App_MessageBlockManager::instance()->Close(pMBHead);
                App_MessageBlockManager::instance()->Close(pMBBody);
                return false;
            }
        }
    }
    else
    {
        ACE_Message_Block* pMbData = App_MessageBlockManager::instance()->Create(u4Len);
        memcpy_safe(pData, u4Len, pMbData->wr_ptr(), u4Len);
        pMbData->wr_ptr(u4Len);

        //以数据流处理
        if (false == Udp_Common_Recv_Stream(u4ConnectID, pMbData, &m_objPacketParse, m_pPacketParseInfo))
        {
            return false;
        }

        App_MessageBlockManager::instance()->Close(pMbData);
    }

	//处理数据包
	if (false == Udp_Common_Send_WorkThread(u4ConnectID, &m_objPacketParse, addrRemote, m_addrLocal, m_atvInput))
	{
		return false;
	}

    m_u4RecvSize += u4Len;
    m_u4RecvPacketCount++;

    return true;
}

int CReactorUDPHander::Init_Open_Address(const ACE_INET_Addr& AddrRemote)
{
    m_addrLocal = AddrRemote;

    //按照线程初始化统计模块的名字
    char szName[MAX_BUFF_50] = { '\0' };
    sprintf_safe(szName, MAX_BUFF_50, "发送线程");
    m_CommandAccount.InitName(szName, GetXmlConfigAttribute(xmlCommandAccount)->MaxCommandCount);

    //初始化统计模块功能
    m_CommandAccount.Init(GetXmlConfigAttribute(xmlCommandAccount)->Account,
                          GetXmlConfigAttribute(xmlCommandAccount)->FlowAccount,
                          GetXmlConfigAttribute(xmlThreadInfo)->DisposeTimeout);

	m_pBlockMessage = new ACE_Message_Block(GetXmlConfigAttribute(xmlSendInfo)->MaxBlockSize);
	m_pBlockRecv    = new ACE_Message_Block(GetXmlConfigAttribute(xmlRecvInfo)->RecvBuffSize);

    //设置发送超时时间（因为UDP如果客户端不存在的话，sendto会引起一个recv错误）
    //在这里设置一个超时，让个recv不会无限等下去
    struct timeval timeout = { MAX_RECV_UDP_TIMEOUT, 0 };
    ACE_OS::setsockopt(m_skRemote.get_handle(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    //获得PacketParse解析器
    m_pPacketParseInfo = App_PacketParseLoader::instance()->GetPacketParseInfo(m_u4PacketParseInfoID);

    return 0;
}

void CReactorUDPHander::SaveSendInfo(uint32 u4Len)
{
    m_atvOutput = ACE_OS::gettimeofday();
    m_u4SendSize += u4Len;
    m_u4SendPacketCount++;
}

void CReactorUDPHander::Send_Hander_Event(uint32 u4ConnandID, uint8 u1Option, ACE_INET_Addr addrRemote)
{
	_MakePacket objMakePacket;

	objMakePacket.m_u4ConnectID     = u4ConnandID;
	objMakePacket.m_pPacketParse    = nullptr;
	objMakePacket.m_u1Option        = u1Option;
	objMakePacket.m_AddrRemote      = addrRemote;
	objMakePacket.m_u4PacketParseID = m_u4PacketParseInfoID;
	objMakePacket.m_pHandler        = this;
	objMakePacket.m_tvRecv          = m_atvInput;
	objMakePacket.m_emPacketType    = EM_CONNECT_IO_TYPE::CONNECT_IO_UDP;
    objMakePacket.m_AddrListen      = m_addrLocal;

	Send_MakePacket_Queue(objMakePacket);
}

void CReactorUDPHander::GetCommandData(uint16 u2CommandID, _CommandData& objCommandData)
{
    const _CommandData* pCommandData = m_CommandAccount.GetCommandData(u2CommandID);

    if(pCommandData != nullptr)
    {
        objCommandData += (*pCommandData);
    }
}

void CReactorUDPHander::GetFlowInfo(uint32& u4FlowIn, uint32& u4FlowOut)
{
    u4FlowIn  = m_CommandAccount.GetFlowIn();
    u4FlowOut = m_CommandAccount.GetFlowOut();
}
