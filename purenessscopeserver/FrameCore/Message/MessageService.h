#ifndef _MESSAGESERVICE_H
#define _MESSAGESERVICE_H

#include "ace/Synch.h"
#include "ace/Malloc_T.h"
#include "ace/Singleton.h"
#include "ace/Thread_Mutex.h"
#include "ace/Date_Time.h"

#include "BaseTask.h"
#include "Message.h"
#include "MessageManager.h"
#include "LogManager.h"
#include "ThreadInfo.h"
#include "BuffPacket.h"
#include "TimerManager.h"
#include "WorkThreadAI.h"
#include "MessageDyeingManager.h"
#include "ObjectLru.h"
#include "PerformanceCounter.h"
#include "BuffPacketManager.h"
#include "PacketParsePool.h"

#if PSS_PLATFORM == PLATFORM_WIN
#include "WindowsCPU.h"
#else
#include "LinuxCPU.h"
#endif

enum class MESSAGE_SERVICE_THREAD_STATE
{
    THREAD_RUN = 0,               //线程正常运行
    THREAD_MODULE_UNLOAD,         //模块重载，需要线程支持此功能
    THREAD_STOP,                  //线程停止
};

class CWorkThread_Packet_Info
{
public:
    uint8  m_u1ThreadID  = 0;
    uint32 m_u4PacketIn  = 0;
    uint32 m_u4PacketOut = 0;
    uint32 m_u4RecvSize  = 0;
    uint32 m_u4SendSize  = 0;
    
    uint32 m_u4Minute    = 0;    //当前时间，按照分钟统计

    void Clear()
    {
        m_u4PacketIn  = 0;
        m_u4PacketOut = 0;
        m_u4RecvSize  = 0;
        m_u4SendSize  = 0;
    }
};

//工作线程数据包和数据量统计
class CWorkThread_Process
{
public:
    CWorkThread_Packet_Info m_objPacketCounter;

    void Clear(uint32 u4NowMinite)
    {
        if (u4NowMinite != m_objPacketCounter.m_u4Minute)
        {
            m_objPacketCounter.Clear();
            m_objPacketCounter.m_u4Minute = u4NowMinite;
        }
    }

    void AddPacketIn(uint32 m_u4PacketIn, const ACE_Time_Value tvNow)
    {
        uint32 u4NowMinite = (uint32)(tvNow.sec() / 60);
        Clear(u4NowMinite);
        
        m_objPacketCounter.m_u4PacketIn++;
        m_objPacketCounter.m_u4RecvSize = m_u4PacketIn;
    }

    void AddPacketOut(uint32 m_u4PacketOut, const ACE_Time_Value tvNow)
    {
        uint32 u4NowMinite = (uint32)(tvNow.sec() / 60);
        Clear(u4NowMinite);

        m_objPacketCounter.m_u4PacketOut++;
        m_objPacketCounter.m_u4SendSize = m_u4PacketOut;
    }

    CWorkThread_Packet_Info GetCurrInfo(const ACE_Time_Value tvNow)
    {
        uint32 u4NowMinite = (uint32)(tvNow.sec() / 60);
        Clear(u4NowMinite);

        return m_objPacketCounter;
    }
};

//工作线程时序模式
class CMessageService : public ACE_Task<ACE_MT_SYNCH>
{
public:
    CMessageService();

    virtual int handle_signal (int signum,
                               siginfo_t*  = 0,
                               ucontext_t* = 0);

    int open();
    virtual int svc (void);
    int Close();

    void Init(uint32 u4ThreadID, uint32 u4MaxQueue = MAX_MSG_THREADQUEUE, uint32 u4LowMask = MAX_MSG_MASK, uint32 u4HighMask = MAX_MSG_MASK, bool blIsCpuAffinity = false);

    bool Start();

    bool PutMessage(CWorkThreadMessage* pMessage);
    bool PutUpdateCommandMessage(uint32 u4UpdateIndex);

    _ThreadInfo* GetThreadInfo();
    bool SaveThreadInfoData(const ACE_Time_Value& tvNow);   //记录当前工作线程状态信息日志

    void GetAIInfo(_WorkThreadAIInfo& objAIInfo) const;           //得到所有工作线程的AI配置
    void GetAITO(vecCommandTimeout& objTimeout) const;            //得到所有的AI超时数据包信息
    void GetAITF(vecCommandTimeout& objTimeout) const;            //得到所有的AI封禁数据包信息
    void SetAI(uint8 u1AI, uint32 u4DisposeTime, uint32 u4WTCheckTime, uint32 u4WTStopTime);  //设置AI

    void SetThreadState(MESSAGE_SERVICE_THREAD_STATE emState);             //设置线程状态
    MESSAGE_SERVICE_THREAD_STATE GetThreadState() const;                   //得到当前线程状态
    THREADSTATE GetStepState() const;                                      //得到当前步数相关信息
    uint32 GetUsedMessageCount();                                          //得到正在使用的Message对象个数

    uint32 GetThreadID() const;

    uint32 GetHandlerCount();                                              //得到当前线程中Handler的个数

    void CopyMessageManagerList();                                         //从MessageManager中获得信令列表副本

    CWorkThreadMessage* CreateMessage();
    void DeleteMessage(CWorkThreadMessage* pMessage);

    void GetFlowPortList(const ACE_Time_Value tvNow, vector<CWorkThread_Packet_Info>& vec_Port_Data_Account); //得到当前列表描述信息

    bool Synchronize_SendPostMessage(CWorkThread_Handler_info* pHandlerInfo, const ACE_Time_Value tvMessage);
    bool SendPostMessage(CSendMessageInfo objSendMessageInfo);                //发送数据
    bool SendCloseMessage(uint32 u4ConnectID);                                //关闭指定链接 

    _ClientIPInfo GetClientIPInfo(uint32 u4ConnectID);                        //得到客户端接入端口
    _ClientIPInfo GetLocalIPInfo(uint32 u4ConnectID);                         //得到客户端监听端口

    void Check_Handler_Recv_Timeout();                                        //检查终端连接时间超时
    EM_Client_Connect_status GetConnectState(uint32 u4ConnectID);             //得到当前连接状态 

private:
    bool ProcessRecvMessage(CWorkThreadMessage* pMessage, uint32 u4ThreadID); //处理接收事件
    bool ProcessSendMessage(CWorkThreadMessage* pMessage, uint32 u4ThreadID); //处理发送事件
    bool ProcessSendClose(CWorkThreadMessage* pMessage, uint32 u4ThreadID);   //处理发送事件
    bool ProcessSendIsLog(CWorkThreadMessage* pMessage, uint32 u4ThreadID);   //处理发送事件
    void CloseCommandList();                                                  //清理当前信令列表副本
    CClientCommandList* GetClientCommandList(uint16 u2CommandID);
    bool DoMessage(IMessage* pMessage, uint16& u2CommandID, uint16& u2Count, bool& bDeleteFlag);

    virtual int CloseMsgQueue();

    void UpdateCommandList(const ACE_Message_Block* pmb);        //更新指令列表
    bool Dispose_Queue();                                  //队列消费

    uint32                         m_u4ThreadID         = 0;                     //当前线程ID
    uint32                         m_u4MaxQueue         = MAX_MSG_THREADQUEUE;   //线程中最大消息对象个数
    uint32                         m_u4HighMask         = 0;        
    uint32                         m_u4LowMask          = 0;
    uint32                         m_u4WorkQueuePutTime = 0;                     //入队超时时间
    uint32                         m_u4MaxRecvWait      = 0;                     //最大等待时间  
    uint16                         m_u2ThreadTimeOut    = 0;
    uint8                          m_u1PacketCounter    = 0;                     //是否开启性能统开关,0是关闭，1是开启
    bool                           m_blRun              = false;                 //线程是否在运行
    bool                           m_blIsCpuAffinity    = false;                 //是否CPU绑定
    bool                           m_blOverload         = false;                 //是否过载   

    MESSAGE_SERVICE_THREAD_STATE   m_emThreadState      = MESSAGE_SERVICE_THREAD_STATE::THREAD_STOP; //当前工作线程状态

    CBuffPacket                    m_objBuffSendPacket;

    _ThreadInfo                    m_ThreadInfo;           //当前线程信息
    CWorkThreadAI                  m_WorkThreadAI;         //线程自我监控的AI逻辑
    CWorkThread_Process            m_objWorkThreadProcess; //统计线程一分钟的数据包处理量

    CDeviceHandlerPool             m_DeviceHandlerPool;    //对象池 

    CHashTable<CClientCommandList>                      m_objClientCommandList;  //可执行的信令列表
    CHashTable<CWorkThread_Handler_info>                m_objHandlerList;        //对应的Handler列表

    ACE_Thread_Mutex m_mutex;
    ACE_Condition<ACE_Thread_Mutex> m_cond;
    CPerformanceCounter m_PerformanceCounter;
};

//add by freeeyes
//添加线程管理，用户可以创建若干个ACE_Task，每个Task对应一个线程，一个Connectid只对应一个线程。
class CMessageServiceGroup : public ACE_Task<ACE_MT_SYNCH>
{
public:
    CMessageServiceGroup();

    virtual int handle_timeout(const ACE_Time_Value& tv, const void* arg);

    bool Init(uint32 u4ThreadCount = MAX_MSG_THREADCOUNT, uint32 u4MaxQueue = MAX_MSG_THREADQUEUE, uint32 u4LowMask = MAX_MSG_MASK);
    bool PutMessage(CWorkThreadMessage* pMessage);                                                     //发送到相应的线程去处理
    bool PutUpdateCommandMessage(uint32 u4UpdateIndex);                                      //发送消息同步所有的工作线程命令副本
    void Close();

    bool Start();
    CThreadInfoList* GetThreadInfo();
    uint32 GetUsedMessageCount();

    uint32 GetWorkThreadCount() const;                                                        //得到当前工作线程的数量
    uint32 GetWorkThreadIDByIndex(uint32 u4Index);                                            //得到指定工作线程的线程ID
    void GetWorkThreadAIInfo(vecWorkThreadAIInfo& objvecWorkThreadAIInfo);                    //得到线程工作AI配置信息
    void GetAITO(vecCommandTimeout& objTimeout);                                              //得到所有的AI超时数据包信息
    void GetAITF(vecCommandTimeout& objTimeout);                                              //得到所有的AI封禁数据包信息
    void SetAI(uint8 u1AI, uint32 u4DisposeTime, uint32 u4WTCheckTime, uint32 u4WTStopTime);  //设置AI

    CWorkThreadMessage* CreateMessage(uint32 u4ConnectID, EM_CONNECT_IO_TYPE u1PacketType);   //从子线程中获取一个Message对象
    void DeleteMessage(CWorkThreadMessage* pMessage);                                         //从子线程中回收一个Message对象

    void CopyMessageManagerList();                                                            //从MessageManager中获得信令列表副本

    void AddDyringIP(const char* pClientIP, uint16 u2MaxCount);                               //染色指定的IP
    bool AddDyeingCommand(uint16 u2CommandID, uint16 u2MaxCount);                             //染色指定的CommandID
    void GetDyeingCommand(vec_Dyeing_Command_list& objList) const;                            //获得当前命令染色状态

    void GetFlowPortList(vector<CWorkThread_Packet_Info>& vec_Port_Data_Account);             //得到当前列表描述信息
    bool CheckCPUAndMemory(bool blTest = false);                                              //检查CPU和内存

    bool Send_Post_Message(CSendMessageInfo objSendMessageInfo);                              //发送数据信息
    bool Send_Close_Message(uint32 u4ConnectID);                                              //关闭客户端连接
    _ClientIPInfo GetClientIPInfo(uint32 u4ConnectID);                                        //得到指定链接的客户单ID 
    _ClientIPInfo GetLocalIPInfo(uint32 u4ConnectID);                                         //得到监听的IP信息
    uint32 GetHandlerCount();                                                                 //得到当前连接总数
    EM_Client_Connect_status GetConnectState(uint32 u4ConnectID);                             //得到当前连接状态 

private:
    uint32 GetWorkThreadID(uint32 u4ConnectID, EM_CONNECT_IO_TYPE emPacketType);              //根据操作类型和ConnectID计算出那个工作线程ID

    bool StartTimer();
    bool KillTimer();

    bool CheckRecvTimeout();                                                                  //检查所有的超时链接
    bool CheckWorkThread(const ACE_Time_Value& tvNow);                                        //检查所有的工作线程状态
    bool CheckPacketParsePool() const;                                                        //检查正在使用的消息解析对象
    bool CheckPlugInState() const;                                                            //检查所有插件状态
   
	typedef vector<CMessageService*> vecMessageService;
	vecMessageService                                   m_vecMessageService;
    uint32                                              m_u4MaxQueue           = 0;              //线程中最大消息对象个数
    uint32                                              m_u4HighMask           = 0;              //线程高水位
    uint32                                              m_u4LowMask            = 0;              //线程低水位
    uint32                                              m_u4TimerID            = 0;              //定时器ID
    uint16                                              m_u2ThreadTimeCheck    = 0;              //线程自检时间
    uint16                                              m_u2CurrThreadID       = 0;              //当前轮询到的线程ID
    uint16                                              m_u2CpuNumber          = 0;              //当前CPU的核数
    CThreadInfoList                                     m_objAllThreadInfo;        //当前所有线程信息
    CMessageDyeingManager                               m_objMessageDyeingManager; //数据染色类
    ACE_Recursive_Thread_Mutex                          m_ThreadLock;              //用于线程操作的线程锁，保证CurrThreadID的数据正常
};

typedef ACE_Singleton<CMessageServiceGroup, ACE_Null_Mutex> App_MessageServiceGroup;
#endif
