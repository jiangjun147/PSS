#include "Unit_EChartlog.h"

#ifdef _CPPUNIT_TEST

CUnit_EChartlog::CUnit_EChartlog() : m_pEchartlog(NULL)
{
}

void CUnit_EChartlog::setUp(void)
{
    m_pEchartlog = App_Echartlog::instance();
}

void CUnit_EChartlog::tearDown(void)
{
    OUR_DEBUG((LM_INFO, "[CUnit_EChartlog::tearDown]is Close."));
}

void CUnit_EChartlog::Test_EChartlog(void)
{
    bool blRet = false;

    bool nRet = m_pEchartlog->Writelog("./", "testchart.txt", "���Ա���", "15");

    if (true != nRet)
    {
        blRet = true;
        CPPUNIT_ASSERT_MESSAGE("[CUnit_EChartlog]m_pTMService->AddMessage is fail.", true == blRet);
        return;
    }

    OUR_DEBUG((LM_INFO, "[CUnit_EChartlog]objActiveTimer is finish.\n"));
}

#endif
