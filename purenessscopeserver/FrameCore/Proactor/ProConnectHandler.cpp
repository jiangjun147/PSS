#include "ProConnectHandler.h"

CProConnectHandler::CProConnectHandler(void) : m_u2LocalPort(0), m_u4SendCheckTime(0)
{
    m_u4ConnectID          = 0;
    m_u4AllRecvSize        = 0;
    m_u4AllSendSize        = 0;
    m_u4HandlerID          = 0;
    m_u2MaxConnectTime     = 0;
    m_u1ConnectState       = CONNECTSTATE::CONNECT_INIT;
    m_u1SendBuffState      = CONNECTSTATE::CONNECT_SENDNON;
    m_u4MaxPacketSize      = MAX_MSG_PACKETLENGTH;
    m_u4RecvQueueCount     = 0;
    m_pBlockMessage        = nullptr;
    m_u2RecvQueueTimeout   = MAX_QUEUE_TIMEOUT * 1000;  //目前因为记录的是微秒，所以这里相应的扩大1000倍
    m_u2TcpNodelay         = TCP_NODELAY_ON;
    m_u4SendMaxBuffSize    = 5*1024;
    m_nHashID              = 0;
    m_szConnectName[0]     = '\0';
    m_blIsLog              = false;
    m_szLocalIP[0]         = '\0';
    m_u4PacketParseInfoID  = 0;
    m_u4PacketDebugSize    = 0;
    m_pPacketDebugData     = nullptr;
    m_emIOType             = EM_IO_TYPE::NET_INPUT;
    m_pFileTest            = nullptr;
}

CProConnectHandler::~CProConnectHandler(void)
{
    if (nullptr != m_pBlockMessage)
    {
        m_pBlockMessage->release();
    }

    if (nullptr != m_pBlockRecv)
    {
        m_pBlockRecv->release();
    }

    SAFE_DELETE_ARRAY(m_pPacketDebugData);
    m_u4PacketDebugSize = 0;
}

void CProConnectHandler::Init(uint16 u2HandlerID)
{
    m_u4HandlerID = u2HandlerID;
    m_u2MaxConnectTime = GetXmlConfigAttribute(xmlClientInfo)->MaxConnectTime;
    m_u4MaxPacketSize = GetXmlConfigAttribute(xmlRecvInfo)->RecvBuffSize;
    m_u2TcpNodelay = GetXmlConfigAttribute(xmlSendInfo)->TcpNodelay;

    m_u2RecvQueueTimeout = GetXmlConfigAttribute(xmlRecvInfo)->RecvQueueTimeout * 1000;

    if (m_u2RecvQueueTimeout == 0)
    {
        m_u2RecvQueueTimeout = MAX_QUEUE_TIMEOUT * 1000;
    }

    m_u4SendMaxBuffSize = GetXmlConfigAttribute(xmlSendInfo)->MaxBlockSize;

    m_pPacketDebugData = new char[GetXmlConfigAttribute(xmlServerType)->DebugSize];
    m_u4PacketDebugSize = GetXmlConfigAttribute(xmlServerType)->DebugSize / 5;

    m_pBlockMessage = new ACE_Message_Block(m_u4SendMaxBuffSize);
    m_pBlockRecv    = new ACE_Message_Block(m_u4MaxPacketSize);

    m_RecvCounter.init("RecvThread", 10000);
    m_SendCounter.init("SendThread", 10000);
}


uint32 CProConnectHandler::GetHandlerID()
{
    return m_u4HandlerID;
}

void CProConnectHandler::Close(uint32 u4ConnectID)
{
    //调用连接断开消息，通知PacketParse接口
    m_pPacketParseInfo->DisConnect(u4ConnectID);

    m_Reader.cancel();
    m_Writer.cancel();

    ACE_OS::shutdown(this->handle(), SD_BOTH);

    OUR_DEBUG((LM_ERROR, "[CProConnectHandler::Close](0x%08x)Close(ConnectID=%d), Recv=%d,Send=%d OK.\n", 
        this, 
        GetConnectID(),
        m_u4AllRecvSize,
        m_u4AllSendSize));

    //清理转发接口
    if ("" != m_strDeviceName)
    {
        App_ForwardManager::instance()->DisConnectRegedit(m_addrRemote.get_host_addr(), 
            m_addrRemote.get_port_number(), 
            ENUM_FORWARD_TYPE::ENUM_FORWARD_TCP_CLINET);
        m_strDeviceName = "";
    }

    //将对象指针放入空池中
    App_ProConnectHandlerPool::instance()->Delete(this);
}

void CProConnectHandler::SetConnectID(uint32 u4ConnectID)
{
    m_u4ConnectID = u4ConnectID;
}

uint32 CProConnectHandler::GetConnectID()
{
    return m_u4ConnectID;
}

void CProConnectHandler::addresses (const ACE_INET_Addr& remote_address, const ACE_INET_Addr& local_address)
{
    m_addrRemote = remote_address;
}

uint32 CProConnectHandler::file_open(IFileTestManager* pFileTest)
{
    //暂不实现了

    return GetConnectID();
}

int CProConnectHandler::handle_write_file_stream(const char* pData, uint32 u4Size, uint8 u1ParseID)
{
    //暂不实现，感觉暂时没用

    return 0;
}

void CProConnectHandler::open(ACE_HANDLE h, ACE_Message_Block&)
{
    m_atvConnect      = ACE_OS::gettimeofday();
    m_atvInput        = m_atvConnect;
    m_atvOutput       = m_atvConnect;
    m_atvSendAlive    = m_atvConnect;

    m_u4AllRecvSize       = 0;
    m_u4AllSendSize       = 0;
    m_u4RecvQueueCount    = 0;
    m_blIsLog             = false;
    m_szConnectName[0]    = '\0';
    m_emIOType            = EM_IO_TYPE::NET_INPUT;

    m_pPacketParseInfo = App_PacketParseLoader::instance()->GetPacketParseInfo(m_u4PacketParseInfoID);

    if (nullptr == m_pPacketParseInfo)
    {
        //如果解析器不存在，则直接断开连接
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::open](%s)can't find PacketParseInfo.\n", m_addrRemote.get_host_addr()));
        Close(GetConnectID());
        return;
    }

    if(App_ForbiddenIP::instance()->CheckIP(m_addrRemote.get_host_addr()) == false)
    {
        //在禁止列表中，不允许访问
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::open]IP Forbidden(%s).\n", m_addrRemote.get_host_addr()));
        Close(GetConnectID());
        return;
    }

    //检查单位时间链接次数是否达到上限
    if(false == App_IPAccount::instance()->AddIP((string)m_addrRemote.get_host_addr()))
    {
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::open]IP(%s) connect frequently.\n", m_addrRemote.get_host_addr()));
        App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), GetXmlConfigAttribute(xmlIP)->Timeout);

        //发送告警邮件
        AppLogManager::instance()->WriteToMail_i(LOG_SYSTEM_CONNECT,
                GetXmlConfigAttribute(xmlClientData)->MailID,
                "Alert IP",
                "[CProConnectHandler::open] IP is more than IP Max,");

        Close(GetConnectID());
        return;
    }

    if(m_u2TcpNodelay == TCP_NODELAY_OFF)
    {
        //如果设置了禁用Nagle算法，则这里要禁用。
        int nOpt = 1;
        ACE_OS::setsockopt(h, IPPROTO_TCP, TCP_NODELAY, (char* )&nOpt, sizeof(int));
    }

    this->handle(h);

    //默认别名是IP地址
    SetConnectName(m_addrRemote.get_host_addr());

    if(this->m_Reader.open(*this, h, 0, proactor()) == -1 ||
       this->m_Writer.open(*this, h, 0, proactor()) == -1)
    {
        OUR_DEBUG((LM_DEBUG,"[CProConnectHandler::open] m_reader or m_reader == 0.\n"));
        Close(GetConnectID());
        return;
    }

    m_u1ConnectState = CONNECTSTATE::CONNECT_OPEN;

    //告诉PacketParse连接应建立
    m_pPacketParseInfo->Connect(GetConnectID(), GetClientIPInfo(), GetLocalIPInfo());

    //发送链接建立消息
    Send_Hander_Event(PACKET_CONNECT);

    OUR_DEBUG((LM_DEBUG,"[CProConnectHandler::open]Open(%d) Connection from [%s:%d](0x%08x).\n", GetConnectID(), m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), this));

    //查看是否存在转发服务
    m_strDeviceName = App_ForwardManager::instance()->ConnectRegedit(m_addrRemote.get_host_addr(),
        m_addrRemote.get_port_number(),
        ENUM_FORWARD_TYPE::ENUM_FORWARD_TCP_CLINET,
        dynamic_cast<IDeviceHandler*>(this));

    //获得接收缓冲区大小
    Get_Recv_length();

    return;
}

void CProConnectHandler::handle_read_stream(const ACE_Asynch_Read_Stream::Result& result)
{
    ACE_Message_Block& mb = result.message_block();
    uint32 u4PacketLen = (uint32)result.bytes_transferred();
    int nTran = (int)result.bytes_transferred();

    if(!result.success() || result.bytes_transferred() == 0 || this->handle() == ACE_INVALID_HANDLE)
    {
        //链接断开
        //清理PacketParse
        ClearPacketParse();

        //发送链接关闭消息
        Send_Hander_Event(PACKET_CDISCONNECT);

        return;
    }

    //查看是否需要转发
    if ("" != m_strDeviceName)
    {
        App_ForwardManager::instance()->SendData(m_strDeviceName, &mb);
        mb.reset();
        return;
    }

    m_atvInput = ACE_OS::gettimeofday();

    Output_Debug_Data(&mb, LOG_SYSTEM_DEBUG_CLIENTRECV);

    if (false == Dispose_Recv_buffer())
    {
		//清理PacketParse
		ClearPacketParse();

		//发送链接关闭消息
		Send_Hander_Event(PACKET_CDISCONNECT);

		return;
    }

    Get_Recv_length();

    return;
}

void CProConnectHandler::handle_write_stream(const ACE_Asynch_Write_Stream::Result& result)
{
    if(!result.success() || result.bytes_transferred()==0)
    {
        //发送失败
        int nErrno = errno;
        OUR_DEBUG ((LM_DEBUG,"[CProConnectHandler::handle_write_stream] Connectid=[%d] begin(%d)...\n",GetConnectID(), nErrno));

        AppLogManager::instance()->WriteLog_i(LOG_SYSTEM_CONNECT, "WriteError [%s:%d] nErrno = %d  result.bytes_transferred() = %d, ",
                                              m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), nErrno,
                                              result.bytes_transferred());

        OUR_DEBUG((LM_DEBUG,"[CProConnectHandler::handle_write_stream] Connectid=[%d] finish ok...\n", GetConnectID()));
        m_atvOutput = ACE_OS::gettimeofday();

        //错误消息回调
        App_MakePacket::instance()->PutSendErrorMessage(GetConnectID(), &result.message_block(), m_atvOutput);

        return;
    }
    else
    {
        //发送成功
        m_u4AllSendSize += (uint32)result.bytes_to_write();

        //清理发送数据信息
        App_MessageBlockManager::instance()->Close(&result.message_block());

#ifdef SET_PROFILE_OUTPUT
        m_SendCounter.counter();
#endif

        return;
    }
}

CONNECTSTATE CProConnectHandler::GetConnectState()
{
    return m_u1ConnectState;
}

CONNECTSTATE CProConnectHandler::GetSendBuffState()
{
    return m_u1SendBuffState;
}

bool CProConnectHandler::SendMessage(CSendMessageInfo objSendMessageInfo, uint32& u4PacketSize)
{
    if(nullptr == objSendMessageInfo.pBuffPacket)
    {
        OUR_DEBUG((LM_DEBUG,"[CProConnectHandler::SendMessage] Connectid=[%d] pBuffPacket is nullptr.\n", GetConnectID()));
        return false;
    }

    if (EM_IO_TYPE::NET_INPUT == m_emIOType)
    {
        //如果不是直接发送数据，则拼接数据包
        if (objSendMessageInfo.emSendState == EM_Client_Send_Status::CLIENT_SEND_CACHE)
        {
            return Send_Input_To_Cache(objSendMessageInfo, u4PacketSize);
        }
        else
        {
            //要发送数据到对端
            return Send_Input_To_TCP(objSendMessageInfo, u4PacketSize);
        }
    }
    else
    {
        //文件入口，直接写入日志
        return Write_SendData_To_File(objSendMessageInfo.blDelete, objSendMessageInfo.pBuffPacket);
    }

    return true;
}

bool CProConnectHandler::PutSendPacket(uint32 u4ConnectID, ACE_Message_Block* pMbData, uint32 u4Size, const ACE_Time_Value tvSend)
{
    ACE_Message_Block* pmbSend = App_MessageBlockManager::instance()->Create(u4Size);
    memcpy_safe(pMbData->rd_ptr(),
        u4Size,
        pmbSend->rd_ptr(),
        u4Size);

    pmbSend->wr_ptr(u4Size);

    //如果是DEBUG状态，记录当前发送包的二进制数据
    Output_Debug_Data(pmbSend, LOG_SYSTEM_DEBUG_CLIENTSEND);

    //统计发送数量
    m_atvOutput = tvSend;

    //异步发送方法
    if(0 != m_Writer.write(*pmbSend, pmbSend->length()))
    {

        OUR_DEBUG ((LM_ERROR, "[CProConnectHandler::PutSendPacket] Connectid=%d mb=%d m_writer.write error(%d)!\n", 
            u4ConnectID,
            pMbData->length(), 
            errno));

        //如果发送失败，在这里返回失败，回调给业务逻辑去处理
        ACE_Time_Value tvNow = tvSend;
        App_MakePacket::instance()->PutSendErrorMessage(u4ConnectID, pmbSend, tvNow);
        return false;
    }

    return true;
}

bool CProConnectHandler::Dispose_Recv_buffer()
{
    bool blRet = true;

    //处理数据切包
    while (true)
    {
        if (m_pPacketParseInfo->m_u1PacketParseType == PACKET_WITHHEAD)
        {
            //如果是包头模式
            if (m_pBlockRecv->length() < m_pPacketParseInfo->m_u4OrgLength)
            {
                //没有收全包头，继续接收
                Move_Recv_buffer();
                blRet = true;
                break;
            }
            else
            {
                //处理包头
                ACE_Message_Block* pHead = App_MessageBlockManager::instance()->Create(m_pPacketParseInfo->m_u4OrgLength);
                //pHead->copy(m_pBlockRecv->rd_ptr(), m_pPacketParseInfo->m_u4OrgLength);
                memcpy_safe(m_pBlockRecv->rd_ptr(),
                    m_pPacketParseInfo->m_u4OrgLength, 
                    pHead->wr_ptr(),
                    m_pPacketParseInfo->m_u4OrgLength);
                pHead->wr_ptr(m_pPacketParseInfo->m_u4OrgLength);
                
                if (0 != Dispose_Paceket_Parse_Head(pHead))
                {
                    blRet = false;
                    break;
                }

                //判断处理后的数据包体长度是否包含
                uint32 u4BodyLength = m_objPacketParse.GetPacketBodySrcLen();
                uint32 u4AllPacketLength = u4BodyLength + m_pPacketParseInfo->m_u4OrgLength;
                if (u4AllPacketLength <= (uint32)m_pBlockRecv->length())
                {
                    ACE_Message_Block* pBody = App_MessageBlockManager::instance()->Create(u4BodyLength);
                    //pBody->copy((char* )(m_pBlockRecv->rd_ptr() + m_pPacketParseInfo->m_u4OrgLength), u4BodyLength);
                    memcpy_safe((char*)(m_pBlockRecv->rd_ptr() + m_pPacketParseInfo->m_u4OrgLength),
                        u4BodyLength,
                        pBody->wr_ptr(),
                        u4BodyLength);
                    pBody->wr_ptr(u4BodyLength);

					if (0 != Dispose_Paceket_Parse_Body(pBody, u4BodyLength))
					{
						blRet = false;
						break;
					}

                    //处理完成了一个完整的包，整体偏移一下
                    m_pBlockRecv->rd_ptr(u4AllPacketLength);
                }
                else
                {
                    //没接收完全，继续接收

                    //处理接收的包头结构体的回收,下一次会重新组织包头
                    pHead->release();

                    Move_Recv_buffer();
                    blRet = true;
					break;
                }
            }
        }
        else
        {
            //如果是流模式
            Dispose_Paceket_Parse_Stream(m_pBlockRecv);
            m_pBlockRecv->reset();
        }
    }

    return blRet;
}

void CProConnectHandler::Move_Recv_buffer()
{
	if (m_pBlockRecv->rd_ptr() != m_pBlockRecv->base() && m_pBlockRecv->length() > 0)
	{
		//移动到前面去
		uint32 u4RemainLength = (uint32)m_pBlockRecv->length();
		ACE_Message_Block* pBlockRemain = App_MessageBlockManager::instance()->Create(u4RemainLength);
		memcpy_safe(m_pBlockRecv->rd_ptr(),
			u4RemainLength,
            pBlockRemain->rd_ptr(),
			u4RemainLength);
		pBlockRemain->wr_ptr(u4RemainLength);

		m_pBlockRecv->reset();

		memcpy_safe(pBlockRemain->rd_ptr(),
			u4RemainLength,
            m_pBlockRecv->rd_ptr(),
			u4RemainLength);
		m_pBlockRecv->wr_ptr(u4RemainLength);
		App_MessageBlockManager::instance()->Close(pBlockRemain);
	}
	else if(m_pBlockRecv->length() == 0) //如果全部接受完成才清理
	{
		//全部重置
		m_pBlockRecv->reset();
	}
}

void CProConnectHandler::Send_Hander_Event(uint8 u1Option)
{
    _MakePacket objMakePacket;

    objMakePacket.m_u4ConnectID     = GetConnectID();
    objMakePacket.m_pPacketParse    = nullptr;
    objMakePacket.m_u1Option        = u1Option;
    objMakePacket.m_AddrRemote      = m_addrRemote;
    objMakePacket.m_u4PacketParseID = m_u4PacketParseInfoID;
    objMakePacket.m_pHandler        = this;
    objMakePacket.m_tvRecv          = m_atvInput;
    objMakePacket.m_AddrListen      = m_addrListen;

    Send_MakePacket_Queue(objMakePacket);
}

void CProConnectHandler::Get_Recv_length()
{
    RecvClinetPacket();
}

void CProConnectHandler::Output_Debug_Data(ACE_Message_Block* pMbData, int nLogType)
{
    //如果是DEBUG状态，记录当前接受包的二进制数据
    if (GetXmlConfigAttribute(xmlServerType)->Debug == DEBUG_ON || m_blIsLog == true)
    {
        char szLog[10] = { '\0' };
        int  nDebugSize = 0;
        bool blblMore = false;

        if (pMbData->length() >= m_u4PacketDebugSize)
        {
            nDebugSize = m_u4PacketDebugSize - 1;
            blblMore = true;
        }
        else
        {
            nDebugSize = (int)pMbData->length();
        }

        char* pData = pMbData->rd_ptr();

        for (int i = 0; i < nDebugSize; i++)
        {
            sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
            sprintf_safe(m_pPacketDebugData + 5 * i, MAX_BUFF_1024 - 5 * i, "%s", szLog);
        }

        m_pPacketDebugData[5 * nDebugSize] = '\0';

        if (blblMore == true)
        {
            AppLogManager::instance()->WriteLog_i(nLogType, "[(%s)%s:%d]%s.(数据包过长只记录前200字节)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_pPacketDebugData);
        }
        else
        {
            AppLogManager::instance()->WriteLog_i(nLogType, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_pPacketDebugData);
        }
    }
}

int CProConnectHandler::Dispose_Paceket_Parse_Head(ACE_Message_Block* pmb)
{
    //判断头的合法性
    _Head_Info objHeadInfo;

    bool blStateHead = m_pPacketParseInfo->Parse_Packet_Head_Info(GetConnectID(), pmb, App_MessageBlockManager::instance(), &objHeadInfo, EM_CONNECT_IO_TYPE::CONNECT_IO_TCP);

    if (false == blStateHead)
    {
        //如果包头是非法的，则返回错误，断开连接。
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::handle_read_stream]PacketHead is illegal.\n"));

        //清理PacketParse
        ClearPacketParse();

        //关闭当前连接
        Send_Hander_Event(PACKET_CDISCONNECT);

        return -1;
    }
    else
    {
        if (nullptr == objHeadInfo.m_pmbHead)
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandler::RecvData]ConnectID=%d, objHeadInfo.m_pmbHead is nullptr.\n", GetConnectID()));
        }

        m_objPacketParse.SetPacket_IsHandleHead(false);
        m_objPacketParse.SetPacket_Head_Src_Length(m_pPacketParseInfo->m_u4OrgLength);
        m_objPacketParse.SetPacket_Head_Message(objHeadInfo.m_pmbHead);
        m_objPacketParse.SetPacket_Head_Curr_Length(objHeadInfo.m_u4HeadCurrLen);
        m_objPacketParse.SetPacket_Body_Src_Length(objHeadInfo.m_u4BodySrcLen);
        m_objPacketParse.SetPacket_CommandID(objHeadInfo.m_u2PacketCommandID);
    }

    //这里添加只处理包头的数据
    //如果数据只有包头，不需要包体，在这里必须做一些处理，让数据只处理包头就扔到DoMessage()
    uint32 u4PacketBodyLen = m_objPacketParse.GetPacketBodySrcLen();

    if (u4PacketBodyLen == 0)
    {
        //如果只有包头没有包体，则直接丢到逻辑里处理
        if (false == CheckMessage())
        {
            Send_Hander_Event(PACKET_CDISCONNECT);
            return -1;
        }
    }
    else
    {
        //如果超过了最大包长度，为非法数据
        if (u4PacketBodyLen >= m_u4MaxPacketSize)
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandler::handle_read_stream]u4PacketHeadLen(%d) more than %d.\n", u4PacketBodyLen, m_u4MaxPacketSize));

            //清理PacketParse
            ClearPacketParse();

			//发送链接断开事件
			Send_Hander_Event(PACKET_SDISCONNECT);

            return -1;
        }
    }

    return 0;
}

int CProConnectHandler::Dispose_Paceket_Parse_Body(ACE_Message_Block* pmb, uint32 u4SrcBodyLength)
{
    //接受完整数据完成，开始分析完整数据包
    _Body_Info obj_Body_Info;
    bool blStateBody = m_pPacketParseInfo->Parse_Packet_Body_Info(GetConnectID(),
                       pmb,
                       App_MessageBlockManager::instance(),
                       &obj_Body_Info,
                       EM_CONNECT_IO_TYPE::CONNECT_IO_TCP);

    if (false == blStateBody)
    {
        //如果数据包体非法，断开连接
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::handle_read_stream]SetPacketBody is illegal.\n"));

        //清理PacketParse
        ClearPacketParse();

        //关闭当前连接
		Send_Hander_Event(PACKET_SDISCONNECT);

        return -1;
    }
    else
    {
        m_objPacketParse.SetPacket_Body_Message(obj_Body_Info.m_pmbBody);
        m_objPacketParse.SetPacket_Body_Curr_Length(obj_Body_Info.m_u4BodyCurrLen);

        if (obj_Body_Info.m_u2PacketCommandID > 0)
        {
            m_objPacketParse.SetPacket_CommandID(obj_Body_Info.m_u2PacketCommandID);
        }
    }

    if (false == CheckMessage())
    {
        //关闭当前连接
        Send_Hander_Event(PACKET_SDISCONNECT);

        return -1;
    }

    return 0;
}

int CProConnectHandler::Dispose_Paceket_Parse_Stream(ACE_Message_Block* pCurrMessage)
{
    //以流模式解析
    while (true)
    {
        //处理消息
        uint8 n1Ret = Tcp_Common_Recv_Stream(GetConnectID(), pCurrMessage, &m_objPacketParse, m_u4PacketParseInfoID);

        if (PACKET_GET_NO_ENOUGH == n1Ret)
        {
            //接收的数据不完整，需要继续接收
            break;
        }
        else if (PACKET_GET_ENOUGH == n1Ret)
        {
            //已经接收了完整数据包，扔给工作线程去处理
            if (false == CheckMessage())
            {
                App_MessageBlockManager::instance()->Close(pCurrMessage);

				//关闭当前连接
				Send_Hander_Event(PACKET_SDISCONNECT);

                return -1;
            }

            //看看是否接收完成了
            if (pCurrMessage->length() == 0)
            {
                break;
            }
            else
            {
                //还有数据，继续分析
                continue;
            }
        }
        else
        {
            //数据包为错误包，丢弃处理
            m_objPacketParse.Clear();

			//关闭当前连接
			Send_Hander_Event(PACKET_SDISCONNECT);

            return -1;
        }
    }

    return 0;
}

bool CProConnectHandler::Write_SendData_To_File(bool blDelete, IBuffPacket* pBuffPacket)
{
    //文件入口，直接写入日志
    _File_Message_Param obj_File_Message_Param;
    obj_File_Message_Param.m_addrRemote        = m_addrRemote;
    obj_File_Message_Param.m_blDelete          = blDelete;
    obj_File_Message_Param.m_pFileTest         = m_pFileTest;
    obj_File_Message_Param.m_u4ConnectID       = GetConnectID();
    obj_File_Message_Param.m_u4PacketDebugSize = m_u4PacketDebugSize;

    return Tcp_Common_File_Message(obj_File_Message_Param,
                                   pBuffPacket,
                                   m_szConnectName);
}

bool CProConnectHandler::Send_Input_To_Cache(CSendMessageInfo objSendMessageInfo, uint32& u4PacketSize)
{
    _Input_To_Cache_Param obj_Input_To_Cache_Param;
    obj_Input_To_Cache_Param.m_blDelete            = objSendMessageInfo.blDelete;
    obj_Input_To_Cache_Param.m_emSendType          = objSendMessageInfo.emSendType;
    obj_Input_To_Cache_Param.m_u2CommandID         = objSendMessageInfo.u2CommandID;
    obj_Input_To_Cache_Param.m_u4ConnectID         = m_u4ConnectID;
    obj_Input_To_Cache_Param.m_u4PacketParseInfoID = m_u4PacketParseInfoID;
    obj_Input_To_Cache_Param.m_u4SendMaxBuffSize   = m_u4SendMaxBuffSize;

    return Tcp_Common_Send_Input_To_Cache(obj_Input_To_Cache_Param,
        m_pBlockMessage,
        u4PacketSize,
        objSendMessageInfo.pBuffPacket);
}

bool CProConnectHandler::Send_Input_To_TCP(CSendMessageInfo objSendMessageInfo, uint32& u4PacketSize)
{
    //先判断是否要组装包头，如果需要，则组装在m_pBlockMessage中
    _Send_Packet_Param obj_Send_Packet_Param;
    obj_Send_Packet_Param.m_blDelete            = objSendMessageInfo.blDelete;
    obj_Send_Packet_Param.m_emSendType          = objSendMessageInfo.emSendType;
    obj_Send_Packet_Param.m_u2CommandID         = objSendMessageInfo.u2CommandID;
    obj_Send_Packet_Param.m_u4ConnectID         = GetConnectID();
    obj_Send_Packet_Param.m_u4PacketParseInfoID = m_u4PacketParseInfoID;
    obj_Send_Packet_Param.m_u4SendMaxBuffSize   = m_u4SendMaxBuffSize;

    //拼装数据
    bool blState = Tcp_Common_Make_Send_Packet(obj_Send_Packet_Param,
        objSendMessageInfo.pBuffPacket,
        m_pBlockMessage,
        u4PacketSize);

    if (false == blState)
    {
        return false;
    }

    Recovery_Common_BuffPacket(objSendMessageInfo.blDelete, objSendMessageInfo.pBuffPacket);

    //将消息ID放入MessageBlock
    ACE_Message_Block::ACE_Message_Type objType = ACE_Message_Block::MB_USER + objSendMessageInfo.nMessageID;
    m_pBlockMessage->msg_type(objType);

    blState = PutSendPacket(GetConnectID(), m_pBlockMessage, (uint32)m_pBlockMessage->length(), objSendMessageInfo.tvSendBegin);
    if (true == blState)
    {
        m_pBlockMessage->reset();
    }

    return blState;
}

bool CProConnectHandler::RecvClinetPacket()
{
    //计算接收Buff字节长度
    uint32 u4RecvLength = (uint32)(m_pBlockRecv->size() - m_pBlockRecv->length());

    if(m_Reader.read(*m_pBlockRecv, u4RecvLength) == -1)
    {
        //如果读失败，则关闭连接。
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::RecvClinetPacket] m_reader.read is error(%d)(%d).\n", GetConnectID(), errno));

        ClearPacketParse();

		//关闭当前连接
		Send_Hander_Event(PACKET_SDISCONNECT);

        return false;
    }

    return true;
}

bool CProConnectHandler::CheckMessage()
{
    if(m_objPacketParse.GetMessageHead() != nullptr)
    {
        if(m_objPacketParse.GetMessageBody() == nullptr)
        {
            m_u4AllRecvSize += (uint32)m_objPacketParse.GetMessageHead()->length();
        }
        else
        {
            m_u4AllRecvSize += (uint32)m_objPacketParse.GetMessageHead()->length() + (uint32)m_objPacketParse.GetMessageBody()->length();
        }

        //组织数据
		_MakePacket objMakePacket;

		objMakePacket.m_u4ConnectID     = GetConnectID();
		objMakePacket.m_pPacketParse    = &m_objPacketParse;
		objMakePacket.m_u1Option        = PACKET_PARSE;
		objMakePacket.m_AddrRemote      = m_addrRemote;
		objMakePacket.m_u4PacketParseID = m_u4PacketParseInfoID;
        objMakePacket.m_tvRecv          = m_atvInput;
        objMakePacket.m_AddrListen      = m_addrListen;

        Send_MakePacket_Queue(objMakePacket);

        /*
        //测试代码
        uint32 u4Size = (uint32)(m_objPacketParse.GetMessageHead()->length() + m_objPacketParse.GetMessageBody()->length());
        ACE_Message_Block* pSendmb = App_MessageBlockManager::instance()->Create(u4Size);

        pSendmb->copy(m_objPacketParse.GetMessageHead()->rd_ptr(), m_objPacketParse.GetMessageHead()->length());
        pSendmb->copy(m_objPacketParse.GetMessageBody()->rd_ptr(), m_objPacketParse.GetMessageBody()->length());

        App_MessageBlockManager::instance()->Close(m_objPacketParse.GetMessageHead());
        App_MessageBlockManager::instance()->Close(m_objPacketParse.GetMessageBody());

        PutSendPacket(pSendmb, pSendmb->length(), m_atvInput);

        App_MessageBlockManager::instance()->Close(pSendmb);
        */

		m_objPacketParse.Clear();
		
#ifdef SET_PROFILE_OUTPUT        
        m_RecvCounter.counter();
#endif
    }
    else
    {
        OUR_DEBUG((LM_ERROR, "[CProConnectHandler::CheckMessage] ConnectID = %d, m_pPacketParse is nullptr.\n", GetConnectID()));
    }

    return true;
}

_ClientConnectInfo CProConnectHandler::GetClientInfo()
{
    _ClientConnectInfo_Param obj_ClientConnectInfo_Param;
    obj_ClientConnectInfo_Param.m_addrRemote = m_addrRemote;
    obj_ClientConnectInfo_Param.m_atvConnect = m_atvConnect;
    obj_ClientConnectInfo_Param.m_u4AllRecvSize = m_u4AllRecvSize;
    obj_ClientConnectInfo_Param.m_u4AllSendSize = m_u4AllSendSize;
    obj_ClientConnectInfo_Param.m_u4ConnectID = GetConnectID();
    obj_ClientConnectInfo_Param.m_u4RecvQueueCount = m_u4RecvQueueCount;

    return Tcp_Common_ClientInfo(obj_ClientConnectInfo_Param);
}

_ClientIPInfo CProConnectHandler::GetClientIPInfo()
{
    _ClientIPInfo ClientIPInfo;
    ClientIPInfo.m_strClientIP = m_addrRemote.get_host_addr();
    ClientIPInfo.m_u2Port = m_addrRemote.get_port_number();
    return ClientIPInfo;
}

_ClientIPInfo CProConnectHandler::GetLocalIPInfo()
{
    _ClientIPInfo ClientIPInfo;
    ClientIPInfo.m_strClientIP = (string)m_szLocalIP;
    ClientIPInfo.m_u2Port = m_u2LocalPort;
    return ClientIPInfo;
}

void CProConnectHandler::ClearPacketParse()
{
    m_pBlockRecv->reset();
}

char* CProConnectHandler::GetConnectName()
{
    return m_szConnectName;
}

void CProConnectHandler::SetConnectName( const char* pName )
{
    sprintf_safe(m_szConnectName, MAX_BUFF_100, "%s", pName);
}

void CProConnectHandler::SetIsLog(bool blIsLog)
{
    m_blIsLog = blIsLog;
}

bool CProConnectHandler::GetIsLog()
{
    return m_blIsLog;
}

void CProConnectHandler::SetHashID(int nHashID)
{
    m_nHashID = nHashID;
}

int CProConnectHandler::GetHashID()
{
    return m_nHashID;
}

void CProConnectHandler::SetLocalIPInfo(const char* pLocalIP, uint16 u2LocalPort)
{
    sprintf_safe(m_szLocalIP, MAX_BUFF_50, "%s", pLocalIP);
    m_u2LocalPort = u2LocalPort;

    if (ACE_OS::strcmp("INADDR_ANY", pLocalIP) == 0)
    {
        m_addrListen.set(u2LocalPort);
    }
    else
    {
        m_addrListen.set(u2LocalPort, pLocalIP);
    }
}

void CProConnectHandler::PutSendPacketError(ACE_Message_Block* pMbData)
{

}

void CProConnectHandler::SetPacketParseInfoID(uint32 u4PacketParseInfoID)
{
    m_u4PacketParseInfoID = u4PacketParseInfoID;
}

uint32 CProConnectHandler::GetPacketParseInfoID()
{
    return m_u4PacketParseInfoID;
}

bool CProConnectHandler::SendTimeoutMessage()
{
	_MakePacket objMakePacket;

	objMakePacket.m_u4ConnectID     = GetConnectID();
	objMakePacket.m_pPacketParse    = nullptr;
	objMakePacket.m_u1Option        = PACKET_CHEK_TIMEOUT;
	objMakePacket.m_AddrRemote      = m_addrRemote;
	objMakePacket.m_u4PacketParseID = m_u4PacketParseInfoID;
    objMakePacket.m_AddrListen      = m_addrListen;

    Send_MakePacket_Queue(objMakePacket);

    return true;
}

bool CProConnectHandler::Device_Send_Data(const char* pData, ssize_t nLen)
{
    uint16 u2CommandID = 0x0000;

    IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create(__FILE__, __LINE__);
    pBuffPacket->WriteStream(pData, (uint32)nLen);

    EM_SEND_PACKET_PARSE emState       = EM_SEND_PACKET_PARSE::EM_SENDMESSAGE_JAMPNOMAL;
    EM_Client_Send_Status  emSendState = EM_Client_Send_Status::CLIENT_SEND_IMMEDIATLY;
    uint32 u4PacketSize = 0;

    CSendMessageInfo objSendMessageInfo;

    objSendMessageInfo.u2CommandID = u2CommandID;
    objSendMessageInfo.pBuffPacket = pBuffPacket;
    objSendMessageInfo.emSendType  = emState;
    objSendMessageInfo.emSendState = emSendState;
    objSendMessageInfo.blDelete    = true;

    return SendMessage(objSendMessageInfo, u4PacketSize);
}

CProConnectHandlerPool::CProConnectHandlerPool(void)
{
}

CProConnectHandlerPool::~CProConnectHandlerPool(void)
{
	Close();
}

void CProConnectHandlerPool::Init(int nObjcetCount)
{
	Close();

	//初始化HashTable
	m_objHandlerList.Init((uint32)nObjcetCount);
	m_objHashHandleList.Init((int)nObjcetCount);

	for (int i = 0; i < nObjcetCount; i++)
	{
		CProConnectHandler* pHandler = m_objHandlerList.GetObject(i);

		pHandler->Init(i);

		//将ID和Handler指针的关系存入hashTable
		char szHandlerID[10] = { '\0' };
		sprintf_safe(szHandlerID, 10, "%d", i);
		int nHashPos = m_objHashHandleList.Add_Hash_Data(szHandlerID, pHandler);

		if (-1 != nHashPos)
		{
			pHandler->Init(i);
			pHandler->SetHashID(i);
		}
	}
}

void CProConnectHandlerPool::Close()
{
	//删除hash表空间
	m_objHashHandleList.Close();
}

int CProConnectHandlerPool::GetUsedCount()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	return m_objHashHandleList.Get_Count() - m_objHashHandleList.Get_Used_Count();
}

int CProConnectHandlerPool::GetFreeCount()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	return (int)m_objHashHandleList.Get_Used_Count();
}

CProConnectHandler* CProConnectHandlerPool::Create()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	//在Hash表中弹出一个已使用的数据
	CProConnectHandler* pHandler = m_objHashHandleList.Pop();

    //设置新的ConnectID
    pHandler->SetConnectID(App_ConnectCounter::instance()->CreateCounter());

	//没找到空余的
	return pHandler;
}

bool CProConnectHandlerPool::Delete(CProConnectHandler* pObject)
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	if (nullptr == pObject)
	{
		return false;
	}

	char szHandlerID[10] = { '\0' };
	sprintf_safe(szHandlerID, 10, "%d", pObject->GetHandlerID());
	//这里因为内存是固定的，直接写会Hash原有位置
	bool blState = m_objHashHandleList.Push(szHandlerID, pObject);

	if (false == blState)
	{
		OUR_DEBUG((LM_INFO, "[CProConnectHandlerPool::Delete]szHandlerID=%s(0x%08x).\n", szHandlerID, pObject));
	}

	return true;
}
