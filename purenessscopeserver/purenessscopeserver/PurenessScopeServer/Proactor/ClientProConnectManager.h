#ifndef _PROCONNECTMANAGER_H
#define _PROCONNECTMANAGER_H

//��������Զ�̷������Ĺ�����
//��������һ���ṹ��������ConectHandlerָ��
//add by freeeyes 2010-12-27

#include "ace/INET_Addr.h"
#include "ace/Guard_T.h"

#include "TimerManager.h"
#include "ProAsynchConnect.h"
#include "BaseClientConnectManager.h"
#include "ProactorUDPClient.h"
#include "HashTable.h"
#include "XmlConfig.h"

#define PRO_CONNECT_SERVER_TIMEOUT 100*1000

#define WAIT_FOR_PROCONNECT_FINISH 5000

class CProactorClientInfo
{
public:
    CProactorClientInfo();
    ~CProactorClientInfo();

    bool Init(const char* pIP, int nPort, uint8 u1IPType, int nServerID, CProAsynchConnect* pProAsynchConnect, IClientMessage* pClientMessage);  //��ʼ�����ӵ�ַ�Ͷ˿�
    void SetLocalAddr(const char* pIP, int nPort, uint8 u1IPType);                             //���ñ���IP�Ͷ˿�
    bool Run(bool blIsReadly, EM_Server_Connect_State emState = SERVER_CONNECT_RECONNECT);     //��ʼ����
    bool SendData(ACE_Message_Block* pmblk);                                                   //��������
    int  GetServerID();                                                                        //�õ�������ID
    bool Close();                                                                              //�رշ���������
    void SetProConnectClient(CProConnectClient* pProConnectClient);                            //����ProConnectClientָ��
    CProConnectClient* GetProConnectClient();                                                  //�õ�ProConnectClientָ��
    IClientMessage* GetClientMessage();                                                        //��õ�ǰ����Ϣ����ָ��
    ACE_INET_Addr GetServerAddr();                                                             //��÷������ĵ�ַ
    EM_Server_Connect_State GetServerConnectState();                                           //�õ���ǰ����״̬
    void SetServerConnectState(EM_Server_Connect_State objState);                              //���õ�ǰ����״̬

private:
    ACE_INET_Addr             m_AddrLocal;                //���ص����ӵ�ַ������ָ����
    ACE_INET_Addr             m_AddrServer;               //Զ�̷������ĵ�ַ
    CProConnectClient*        m_pProConnectClient;        //��ǰ���Ӷ���
    CProAsynchConnect*        m_pProAsynchConnect;        //�첽���Ӷ���
    IClientMessage*           m_pClientMessage;           //�ص������࣬�ص����ش���ͷ������ݷ���
    bool                      m_blIsLocal;                //�Ƿ���Ҫ�ƶ����ض˿�
    int                       m_nServerID;                //������ID�����û�������������������
    EM_Server_Connect_State   m_emConnectState;           //����״̬
};

//�����������ӵ������������Ĺ�����
class CClientProConnectManager : public ACE_Task<ACE_MT_SYNCH>, public IClientManager
{
public:
    CClientProConnectManager(void);
    ~CClientProConnectManager(void);

    bool Init(ACE_Proactor* pProactor);                                                                                        //��ʼ��������
    virtual bool Connect(int nServerID, const char* pIP, int nPort, uint8 u1IPType, IClientMessage* pClientMessage);                   //����ָ���ķ�������TCP��
    virtual bool Connect(int nServerID, const char* pIP, int nPort, uint8 u1IPType, const char* pLocalIP, int nLocalPort, uint8 u1LocalIPType, IClientMessage* pClientMessage);  //���ӷ�����(TCP)��ָ�����ص�ַ
    virtual bool ConnectUDP(int nServerID, const char* pIP, int nPort, uint8 u1IPType, EM_UDP_TYPE emType, IClientUDPMessage* pClientUDPMessage);                                //����һ��ָ��UDP�����ӣ�UDP��
    bool ReConnect(int nServerID);                                                                                             //��������һ��ָ���ķ�����(TCP)
    bool CloseByClient(int nServerID);                                                                                         //Զ�̱����ر�(TCP)
    virtual bool Close(int nServerID);                                                                                                 //�ر����ӣ�TCP��
    virtual bool CloseUDP(int nServerID);                                                                                              //�ر����ӣ�UDP��
    bool ConnectErrorClose(int nServerID);                                                                                     //���ڲ����������ʧ�ܣ���ProConnectClient����
    virtual bool SendData(int nServerID, char*& pData, int nSize, bool blIsDelete = true);                                             //�������ݣ�TCP��
    virtual bool SendDataUDP(int nServerID,const char* pIP, int nPort, char*& pMessage, uint32 u4Len, bool blIsDelete = true);   //�������ݣ�UDP��
    bool SetHandler(int nServerID, CProConnectClient* pProConnectClient);                                                      //��ָ����CProConnectClient*�󶨸�nServerID
    virtual IClientMessage* GetClientMessage(int nServerID);                                                                           //���ClientMessage����
    virtual bool StartConnectTask(int nIntervalTime = CONNECT_LIMIT_RETRY);                                                            //�����Զ������Ķ�ʱ��
    virtual void CancelConnectTask();                                                                                                  //�ر�������ʱ��
    virtual void Close();                                                                                                              //�ر���������
    ACE_INET_Addr GetServerAddr(int nServerID);                                                                                //�õ�ָ����������Զ�̵�ַ������Ϣ
    bool SetServerConnectState(int nServerID, EM_Server_Connect_State objState);                                               //����ָ�����ӵ�����״̬
    bool DeleteIClientMessage(IClientMessage* pClientMessage);                                                                 //ɾ��һ���������ڽ�����IClientMessage

    virtual void GetConnectInfo(vecClientConnectInfo& VecClientConnectInfo);      //���ص�ǰ������ӵ���Ϣ��TCP��
    virtual void GetUDPConnectInfo(vecClientConnectInfo& VecClientConnectInfo);   //���ص�ǰ������ӵ���Ϣ��UDP��
    virtual EM_Server_Connect_State GetConnectState(int nServerID);               //�õ�һ����ǰ����״̬
    bool GetServerIPInfo(int nServerID, _ClientIPInfo& objServerIPInfo);  //�õ�һ��nServerID��Ӧ��ServerIP��Ϣ

    virtual int handle_timeout(const ACE_Time_Value& tv, const void* arg);                       //��ʱ���

private:
    bool ConnectTcpInit(int nServerID, const char* pIP, int nPort, uint8 u1IPType, const char* pLocalIP, int nLocalPort, uint8 u1LocalIPType, IClientMessage* pClientMessage, CProactorClientInfo*& pClientInfo);
    bool ConnectUdpInit(int nServerID, CProactorUDPClient*& pProactorUDPClient);

private:
    CProAsynchConnect               m_ProAsynchConnect;
    CHashTable<CProactorClientInfo> m_objClientTCPList;            //TCP�ͻ�������
    CHashTable<CProactorUDPClient>  m_objClientUDPList;            //UDP�ͻ�������
    ACE_Recursive_Thread_Mutex      m_ThreadWritrLock;             //�߳���
    ActiveTimer                     m_ActiveTimer;                 //ʱ�������
    int                             m_nTaskID;                     //��ʱ��⹤��
    bool                            m_blProactorFinish;            //Proactor�Ƿ��Ѿ�ע��
    uint32                          m_u4ConnectServerTimeout;      //���Ӽ��ʱ��
    uint32                          m_u4MaxPoolCount;              //���ӳص�����
};

typedef ACE_Singleton<CClientProConnectManager, ACE_Recursive_Thread_Mutex> App_ClientProConnectManager;

#endif