#include "FileTestManager.h"

CFileTestManager::CFileTestManager(void)
{
    m_bFileTesting     = false;
    m_n4TimerID        = 0;
    m_n4ConnectCount   = 0;
    m_u4ParseID        = 0;
    m_n4ResponseCount  = 0;
    m_n4ExpectTime     = 1000;
    m_n4ContentType    = 1;
    m_n4TimeInterval   = 0;
}

CFileTestManager::~CFileTestManager(void)
{
    Close();

    return;
}

FileTestResultInfoSt CFileTestManager::FileTestStart(const char* szXmlFileTestName)
{
    FileTestResultInfoSt objFileTestResult;

    if(m_bFileTesting)
    {
        OUR_DEBUG((LM_DEBUG, "[CProConnectAcceptManager::FileTestStart]m_bFileTesting:%d.\n",m_bFileTesting));
        objFileTestResult.n4Result = RESULT_ERR_TESTING;
        return objFileTestResult;
    }
    else
    {
        if(!LoadXmlCfg(szXmlFileTestName, objFileTestResult))
        {
            OUR_DEBUG((LM_DEBUG, "[CProConnectAcceptManager::FileTestStart]Loading config file error filename:%s.\n", szXmlFileTestName));
        }
        else
        {
            m_n4TimerID = App_TimerManager::instance()->schedule(this, (void*)NULL, ACE_OS::gettimeofday() + ACE_Time_Value(m_n4TimeInterval), ACE_Time_Value(m_n4TimeInterval));

            if(-1 == m_n4TimerID)
            {
                OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]Start timer error\n"));
                objFileTestResult.n4Result = RESULT_ERR_UNKOWN;
            }
            else
            {
                OUR_DEBUG((LM_ERROR, "[CMainConfig::LoadXmlCfg]Start timer OK.\n"));
                objFileTestResult.n4Result = RESULT_OK;
                m_bFileTesting = true;
            }
        }
    }

    return objFileTestResult;
}

int CFileTestManager::FileTestEnd()
{
    if(m_n4TimerID > 0)
    {
        App_TimerManager::instance()->cancel(m_n4TimerID);
        m_n4TimerID = 0;
        m_bFileTesting = false;
    }

    return 0;
}

void CFileTestManager::HandlerServerResponse(uint32 u4ConnectID)
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);
    char szConnectID[10] = { '\0' };
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);

    ResponseRecordSt* pResponseRecord = m_objResponseRecordList.Get_Hash_Box_Data(szConnectID);

    if (NULL == pResponseRecord)
    {
        OUR_DEBUG((LM_INFO, "[CFileTestManager::HandlerServerResponse]Response time too long m_n4ExpectTimeNo find connectID=%d.\n", u4ConnectID));
        return;
    }

    if(pResponseRecord->m_u1ResponseCount + 1 == m_n4ResponseCount)
    {
        ACE_Time_Value atvResponse = ACE_OS::gettimeofday();

        if(m_n4ExpectTime <= (int)((uint64)atvResponse.get_msec() - pResponseRecord->m_u8StartTime))
        {
            //应答时间超过期望时间限制
            OUR_DEBUG((LM_INFO, "[CFileTestManager::HandlerServerResponse]Response time too long m_n4ExpectTime:%d.\n",m_n4ExpectTime));
        }

#ifndef WIN32
        App_ConnectManager::instance()->Close(u4ConnectID);
#else
        App_ProConnectManager::instance()->Close(u4ConnectID);
#endif
        //回收对象类型
        m_objResponseRecordList.Del_Hash_Data(szConnectID);
        SAFE_DELETE(pResponseRecord);
    }
    else
    {
        pResponseRecord->m_u1ResponseCount += 1;
    }

}

void CFileTestManager::Close()
{
    //关闭定时器
    if (m_n4TimerID > 0)
    {
        App_TimerManager::instance()->cancel(m_n4TimerID);
        m_n4TimerID = 0;
        m_bFileTesting = false;
    }

    //清理m_objResponseRecordList
    vector<ResponseRecordSt*> vecList;
    m_objResponseRecordList.Get_All_Used(vecList);

    int nUsedSize = (int)vecList.size();

    if (nUsedSize > 0)
    {
        for (int i = 0; i < nUsedSize; i++)
        {
            char szConnectID[10] = { '\0' };
            sprintf_safe(szConnectID, 10, "%d", vecList[i]->m_u4ConnectID);
            m_objResponseRecordList.Del_Hash_Data(szConnectID);
            SAFE_DELETE(vecList[i]);
        }
    }

    m_objResponseRecordList.Close();
}

bool CFileTestManager::LoadXmlCfg(const char* szXmlFileTestName, FileTestResultInfoSt& objFileTestResult)
{
    char* pData = NULL;
    OUR_DEBUG((LM_INFO, "[CProConnectAcceptManager::LoadXmlCfg]Filename = %s.\n", szXmlFileTestName));

    if(false == m_MainConfig.Init(szXmlFileTestName))
    {
        OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]File Read Error = %s.\n", szXmlFileTestName));
        objFileTestResult.n4Result = RESULT_ERR_CFGFILE;
        return false;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "Path");

    if(NULL != pData)
    {
        m_strProFilePath = pData;
    }
    else
    {
        m_strProFilePath = "./";
    }

    pData = m_MainConfig.GetData("FileTestConfig", "TimeInterval");

    if(NULL != pData)
    {
        m_n4TimeInterval = (uint8)ACE_OS::atoi(pData);
        objFileTestResult.n4TimeInterval = m_n4TimeInterval;
        OUR_DEBUG((LM_INFO, "[CProConnectAcceptManager::LoadXmlCfg]m_n4TimeInterval = %d.\n", m_n4TimeInterval));
    }
    else
    {
        m_n4TimeInterval = 10;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "ConnectCount");

    if(NULL != pData)
    {
        m_n4ConnectCount = (uint8)ACE_OS::atoi(pData);
        objFileTestResult.n4ConnectNum = m_n4ConnectCount;
        OUR_DEBUG((LM_INFO, "[CProConnectAcceptManager::LoadXmlCfg]m_n4ConnectCount = %d.\n", m_n4ConnectCount));
    }
    else
    {
        m_n4ConnectCount = 10;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "ResponseCount");

    if(NULL != pData)
    {
        m_n4ResponseCount = (uint8)ACE_OS::atoi(pData);
        OUR_DEBUG((LM_INFO, "[CProConnectAcceptManager::LoadXmlCfg]m_n4ResponseCount = %d.\n", m_n4ResponseCount));
    }
    else
    {
        m_n4ResponseCount = 1;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "ExpectTime");

    if(NULL != pData)
    {
        m_n4ExpectTime = (uint8)ACE_OS::atoi(pData);
        OUR_DEBUG((LM_INFO, "[CProConnectAcceptManager::LoadXmlCfg]m_n4ExpectTime = %d.\n", m_n4ExpectTime));
    }
    else
    {
        m_n4ExpectTime = 1000;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "ParseID");

    if(NULL != pData)
    {
        m_u4ParseID = (uint8)ACE_OS::atoi(pData);
    }
    else
    {
        m_u4ParseID = 1;
    }

    pData = m_MainConfig.GetData("FileTestConfig", "ContentType");

    if(NULL != pData)
    {
        m_n4ContentType = (uint8)ACE_OS::atoi(pData);
    }
    else
    {
        m_n4ContentType = 1; //默认是二进制协议
    }

    //命令监控相关配置
    TiXmlElement* pNextTiXmlElementFileName     = NULL;
    TiXmlElement* pNextTiXmlElementDesc         = NULL;

    while(true)
    {
        FileTestDataInfoSt objFileTestDataInfo;
        string strFileName;
        string strFileDesc;

        pData = m_MainConfig.GetData("FileInfo", "FileName", pNextTiXmlElementFileName);

        if(pData != NULL)
        {
            strFileName = m_strProFilePath + pData;
        }
        else
        {
            break;
        }

        pData = m_MainConfig.GetData("FileInfo", "Desc", pNextTiXmlElementDesc);

        if(pData != NULL)
        {
            strFileDesc = pData;
            objFileTestResult.vecProFileDesc.push_back(strFileDesc);
        }
        else
        {
            break;
        }

        //读取数据包文件内容
        int nReadFileRet = ReadTestFile(strFileName.c_str(), m_n4ContentType, objFileTestDataInfo);

        if(RESULT_OK == nReadFileRet)
        {
            m_vecFileTestDataInfoSt.push_back(objFileTestDataInfo);
        }
        else
        {
            objFileTestResult.n4Result = nReadFileRet;
            return false;
        }
    }

    m_MainConfig.Close();

    //初始化连接返回数据数组
    ResponseRecordList();

    objFileTestResult.n4ProNum = static_cast<int>(m_vecFileTestDataInfoSt.size());
    return true;
}

int CFileTestManager::ReadTestFile(const char* pFileName, int nType, FileTestDataInfoSt& objFileTestDataInfo)
{
    ACE_FILE_Connector fConnector;
    ACE_FILE_IO ioFile;
    ACE_FILE_Addr fAddr(pFileName);

    if (fConnector.connect(ioFile, fAddr) == -1)
    {
        OUR_DEBUG((LM_INFO, "[CMainConfig::ReadTestFile]Open filename:%s Error.\n", pFileName));
        return RESULT_ERR_PROFILE;
    }

    ACE_FILE_Info fInfo;

    if (ioFile.get_info(fInfo) == -1)
    {
        OUR_DEBUG((LM_INFO, "[CMainConfig::ReadTestFile]Get file info filename:%s Error.\n", pFileName));
        return RESULT_ERR_PROFILE;
    }

    if (MAX_BUFF_10240 - 1 < fInfo.size_)
    {
        OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]Protocol file too larger filename:%s.\n", pFileName));
        return RESULT_ERR_PROFILE;
    }
    else
    {
        char szFileContent[MAX_BUFF_10240] = { '\0' };
        ssize_t u4Size = ioFile.recv(szFileContent, fInfo.size_);

        if (u4Size != fInfo.size_)
        {
            OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]Read protocol file error filename:%s Error.\n", pFileName));
            return RESULT_ERR_PROFILE;
        }
        else
        {
            if (nType == 0)
            {
                //如果是文本协议
                memcpy_safe(szFileContent, static_cast<uint32>(u4Size), objFileTestDataInfo.m_szData, static_cast<uint32>(u4Size));
                objFileTestDataInfo.m_szData[u4Size] = '\0';
                objFileTestDataInfo.m_u4DataLength = static_cast<uint32>(u4Size);
                OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]u4Size:%d\n", u4Size));
                OUR_DEBUG((LM_INFO, "[CMainConfig::LoadXmlCfg]m_szData:%s\n", objFileTestDataInfo.m_szData));
            }
            else
            {
                //如果是二进制协议
                CConvertBuffer objConvertBuffer;
                //将数据串转换成二进制串
                int nDataSize = MAX_BUFF_10240;
                objConvertBuffer.Convertstr2charArray(szFileContent, static_cast<int>(u4Size), (unsigned char*)objFileTestDataInfo.m_szData, nDataSize);
                objFileTestDataInfo.m_u4DataLength = static_cast<uint32>(nDataSize);
            }
        }
    }

    return RESULT_OK;
}

int CFileTestManager::ResponseRecordList()
{
    //初始化Hash表
    m_objResponseRecordList.Close();
    m_objResponseRecordList.Init((int)m_n4ConnectCount);

    return 0;
}

int CFileTestManager::handle_timeout(const ACE_Time_Value& tv, const void* arg)
{
    ACE_UNUSED_ARG(arg);
    ACE_UNUSED_ARG(tv);

    //首先遍历上一次定时器的执行数据对象是否已经全部释放
    vector<ResponseRecordSt*> vecList;
    m_objResponseRecordList.Get_All_Used(vecList);

    int nUsedSize = (int)vecList.size();

    if (nUsedSize > 0)
    {
        for (int i = 0; i < nUsedSize; i++)
        {
            //在上一个轮询周期，没有结束的对象
            if(m_n4ExpectTime <= (int)((uint64)tv.get_msec() - vecList[i]->m_u8StartTime))
            {
                //超过了执行范围时间
                OUR_DEBUG((LM_INFO, "[CFileTestManager::handle_timeout]Response time too long m_n4ExpectTime:%d.\n", m_n4ExpectTime));
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_ERROR, "[CFileTestManager::handle_timeout]Response time too long connectID=%d, m_n4ExpectTime=%d.",
                                                    vecList[i]->m_u4ConnectID,
                                                    m_n4TimeInterval);
            }
            else
            {
                //超过了定时器时间
                OUR_DEBUG((LM_INFO, "[CFileTestManager::handle_timeout]Response time too long m_n4TimeInterval:%d.\n", m_n4TimeInterval));
                //写入错误日志
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_ERROR, "[CFileTestManager::handle_timeout]Response time too long connectID=%d, m_n4TimeInterval=%d.",
                                                    vecList[i]->m_u4ConnectID,
                                                    m_n4TimeInterval);
            }

            char szConnectID[10] = { '\0' };
            sprintf_safe(szConnectID, 10, "%d", vecList[i]->m_u4ConnectID);
            m_objResponseRecordList.Del_Hash_Data(szConnectID);
            SAFE_DELETE(vecList[i]);
        }
    }

#ifndef WIN32
    CConnectHandler* pConnectHandler = NULL;

    for (int iLoop = 0; iLoop < m_n4ConnectCount; iLoop++)
    {
        pConnectHandler = App_ConnectHandlerPool::instance()->Create();

        if (NULL != pConnectHandler)
        {
            //文件数据包测试不需要用到pProactor对象，因为不需要实际发送数据，所以这里可以直接设置一个固定的pProactor
            ACE_Reactor* pReactor = App_ReactorManager::instance()->GetAce_Client_Reactor(0);
            pConnectHandler->reactor(pReactor);
            pConnectHandler->SetPacketParseInfoID(m_u4ParseID);

            uint32 u4ConnectID = pConnectHandler->file_open(dynamic_cast<IFileTestManager*>(this));

            if (0 != u4ConnectID)
            {
                ResponseRecordSt* pResponseRecord = new ResponseRecordSt();
                pResponseRecord->m_u1ResponseCount = 0;
                pResponseRecord->m_u8StartTime     = tv.get_msec();
                pResponseRecord->m_u4ConnectID = u4ConnectID;

                char szConnectID[10] = { '\0' };
                sprintf_safe(szConnectID, 10, "%d", u4ConnectID);

                if (-1 == m_objResponseRecordList.Add_Hash_Data(szConnectID, pResponseRecord))
                {
                    OUR_DEBUG((LM_INFO, "[CFileTestManager::handle_timeout]Add m_objResponseRecordList error, ConnectID=%d.\n", u4ConnectID));
                }
            }
            else
            {
                OUR_DEBUG((LM_INFO, "[CMainConfig::handle_timeout]file_open error\n"));
            }
        }
    }

    //循环将数据包压入连接对象
    for (int iLoop = 0; iLoop < (int)m_vecFileTestDataInfoSt.size(); iLoop++)
    {
        FileTestDataInfoSt& objFileTestDataInfo = m_vecFileTestDataInfoSt[iLoop];

        vector<ResponseRecordSt*> vecExistList;
        m_objResponseRecordList.Get_All_Used(vecExistList);

        for (int jLoop = 0; jLoop < (int)vecExistList.size(); jLoop++)
        {
            uint32 u4ConnectID = vecExistList[jLoop]->m_u4ConnectID;
            App_ConnectManager::instance()->handle_write_file_stream(u4ConnectID, objFileTestDataInfo.m_szData, objFileTestDataInfo.m_u4DataLength, m_u4ParseID);
        }
    }

#else
    CProConnectHandle* ptrProConnectHandle = NULL;

    for (int iLoop = 0; iLoop < m_n4ConnectCount; iLoop++)
    {
        ptrProConnectHandle = App_ProConnectHandlerPool::instance()->Create();

        if (NULL != ptrProConnectHandle)
        {
            //文件数据包测试不需要用到pProactor对象，因为不需要实际发送数据，所以这里可以直接设置一个固定的pProactor
            ACE_Proactor* pPractor = App_ProactorManager::instance()->GetAce_Client_Proactor(0);
            ptrProConnectHandle->proactor(pPractor);
            ptrProConnectHandle->SetPacketParseInfoID(m_u4ParseID);

            uint32 u4ConnectID = ptrProConnectHandle->file_open(dynamic_cast<IFileTestManager*>(this));

            if (0 != u4ConnectID)
            {
                ResponseRecordSt* pResponseRecord = new ResponseRecordSt();
                pResponseRecord->m_u1ResponseCount = 0;
                pResponseRecord->m_u8StartTime = tv.get_msec();
                pResponseRecord->m_u4ConnectID = u4ConnectID;

                char szConnectID[10] = { '\0' };
                sprintf_safe(szConnectID, 10, "%d", u4ConnectID);

                if (-1 == m_objResponseRecordList.Add_Hash_Data(szConnectID, pResponseRecord))
                {
                    OUR_DEBUG((LM_INFO, "[CFileTestManager::handle_timeout]Add m_objResponseRecordList error, ConnectID=%d.\n", u4ConnectID));
                }
            }
            else
            {
                OUR_DEBUG((LM_INFO, "[CMainConfig::handle_timeout]file_open error\n"));
            }
        }
    }

    for (int iLoop = 0; iLoop < (int)m_vecFileTestDataInfoSt.size(); iLoop++)
    {
        FileTestDataInfoSt& objFileTestDataInfo = m_vecFileTestDataInfoSt[iLoop];

        vector<ResponseRecordSt*> vecExistList;
        m_objResponseRecordList.Get_All_Used(vecExistList);

        for (int jLoop = 0; jLoop < (int)vecExistList.size(); jLoop++)
        {
            uint32 u4ConnectID = vecExistList[jLoop]->m_u4ConnectID;
            App_ProConnectManager::instance()->handle_write_file_stream(u4ConnectID, objFileTestDataInfo.m_szData, objFileTestDataInfo.m_u4DataLength, m_u4ParseID);
        }
    }

#endif

    return 0;
}



