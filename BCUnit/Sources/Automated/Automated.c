/*
 *  BCUnit - A Unit testing framework library for C.
 *  Copyright (C) 2001       Anil Kumar
 *  Copyright (C) 2004-2006  Anil Kumar, Jerry St.Clair
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  Implementation of the Automated Test Interface.
 *
 *  Feb 2002      Initial implementation. (AK)
 *
 *  13/Feb/2002   Added initial automated interface functions to generate
 *                HTML based Run report. (AK)
 *
 *  23/Jul/2002   Changed HTML to XML Format file generation for Automated Tests. (AK)
 *
 *  27/Jul/2003   Fixed a bug which hinders the listing of all failures. (AK)
 *
 *  17-Jul-2004   New interface, doxygen comments, eliminate compiler warnings,
 *                automated_run_tests now assigns a generic file name if
 *                none has been supplied. (JDS)
 *
 *  30-Apr-2005   Added notification of failed suite cleanup function. (JDS)
 *
 *  02-May-2006   Added internationalization hooks.  (JDS)
 *
 *  07-May-2011   Added patch to fix broken xml tags dur to spacial characters in the test name.  (AK)
 */

/** @file
 * Automated test interface with xml result output (implementation).
 */
/** @addtogroup Automated
 @{
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "BCUnit.h"
#include "TestDB.h"
#include "MyMem.h"
#include "Util.h"
#include "TestRun.h"
#include "Automated.h"
#include "BCUnit_intl.h"

#define MAX_FILENAME_LENGTH		1025

/*=================================================================
 *  Global / Static data definitions
 *=================================================================*/
static CU_pSuite f_pRunningSuite = NULL;                    /**< The running test suite. */
static char      f_szDefaultFileRoot[] = "BCUnitAutomated";  /**< Default filename root for automated output files. */
static char      f_szTestListFileName[MAX_FILENAME_LENGTH] = "";   /**< Current output file name for the test listing file. */
static char      f_szTestResultFileName[MAX_FILENAME_LENGTH] = ""; /**< Current output file name for the test results file. */
static FILE*     f_pTestResultFile = NULL;                  /**< FILE pointer the test results file. */

static CU_BOOL   f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;       /**< Flag for keeping track of when a closing xml tag is required. */

static CU_BOOL   bJUnitXmlOutput = CU_FALSE;                /**< Flag for toggling the xml junit output or keeping the original. Off is the default */

static CU_BOOL	 bPartialSuiteJUnitReport = CU_FALSE;		/** Flag for toggling englobing <testsuites> tags. Warning: allows imcomplete JUnit results file where only <testsuite></testsuite> will be present. This is to allow JUnit-xml output for a single suite */

static char _gPackageName[50] = "";

//static time_t    f_testStartTime = 0;                       /**< Start time of current running test suite. */

double startTime = 0.0;
double endTime = 0.0;

/*=================================================================
 *  Static function forward declarations
 *=================================================================*/
static CU_ErrorCode automated_list_all_tests(CU_pTestRegistry pRegistry, const char* szFilename);

static CU_ErrorCode initialize_result_file(const char* szFilename);
static CU_ErrorCode uninitialize_result_file(void);

static void automated_run_all_tests(CU_pTestRegistry pRegistry);

static void automated_test_start_message_handler(const CU_pTest pTest, const CU_pSuite pSuite);
static void automated_test_complete_message_handler(const CU_pTest pTest, const CU_pSuite pSuite, const CU_pFailureRecord pFailure);
static void automated_all_tests_complete_message_handler(const CU_pFailureRecord pFailure);
static void automated_suite_init_failure_message_handler(const CU_pSuite pSuite);
static void automated_suite_cleanup_failure_message_handler(const CU_pSuite pSuite);

CU_TestStartMessageHandler           test_start_handler;
CU_TestCompleteMessageHandler        test_complete_handler;
CU_AllTestsCompleteMessageHandler    all_test_complete_handler;
CU_SuiteInitFailureMessageHandler    suite_init_failure_handler;
CU_SuiteCleanupFailureMessageHandler suite_cleanup_failure_handler;

/*=================================================================
 *  Public Interface functions
 *=================================================================*/
void CU_automated_run_tests(void)
{
  assert(NULL != CU_get_registry());

  /* Ensure output makes it to screen at the moment of a SIGSEGV. */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  /* if a filename root hasn't been set, use the default one */
  if (0 == strlen(f_szTestResultFileName)) {
    CU_set_output_filename(f_szDefaultFileRoot);
  }

  if (CUE_SUCCESS != initialize_result_file(f_szTestResultFileName)) {
    fprintf(stderr, "\n%s", _("ERROR - Failed to create/initialize the result file."));
  }
  else {
    /* get previous handlers in case there was something here */
    test_start_handler = CU_get_test_start_handler();
    test_complete_handler = CU_get_test_complete_handler();
    all_test_complete_handler = CU_get_all_test_complete_handler();
    suite_init_failure_handler = CU_get_suite_init_failure_handler();
    suite_cleanup_failure_handler = CU_get_suite_cleanup_failure_handler();
    /* set up the message handlers for writing xml output */
    CU_set_test_start_handler(automated_test_start_message_handler);
    CU_set_test_complete_handler(automated_test_complete_message_handler);
    CU_set_all_test_complete_handler(automated_all_tests_complete_message_handler);
    CU_set_suite_init_failure_handler(automated_suite_init_failure_message_handler);
    CU_set_suite_cleanup_failure_handler(automated_suite_cleanup_failure_message_handler);

    f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;

    automated_run_all_tests(NULL);

    if (CUE_SUCCESS != uninitialize_result_file()) {
      fprintf(stderr, "\n%s", _("ERROR - Failed to close/uninitialize the result files."));
    }
  }
}

/*------------------------------------------------------------------------*/
void CU_set_output_filename(const char* szFilenameRoot)
{
  const char* szListEnding = "-Listing.xml";
  const char* szResultEnding = "-Results.xml";

  /* Construct the name for the listing file */
  if (NULL != szFilenameRoot) {
    strncpy(f_szTestListFileName, szFilenameRoot, MAX_FILENAME_LENGTH - strlen(szListEnding) - 1);
  }
  else {
    strncpy(f_szTestListFileName, f_szDefaultFileRoot, MAX_FILENAME_LENGTH - strlen(szListEnding) - 1);
  }

  f_szTestListFileName[MAX_FILENAME_LENGTH - strlen(szListEnding) - 1] = '\0';
  strcat(f_szTestListFileName, szListEnding);

  /* Construct the name for the result file */
  if (NULL != szFilenameRoot) {
    strncpy(f_szTestResultFileName, szFilenameRoot, MAX_FILENAME_LENGTH - strlen(szResultEnding) - 1);
  }
  else {
    strncpy(f_szTestResultFileName, f_szDefaultFileRoot, MAX_FILENAME_LENGTH - strlen(szResultEnding) - 1);
  }

  f_szTestResultFileName[MAX_FILENAME_LENGTH - strlen(szResultEnding) - 1] = '\0';
  strcat(f_szTestResultFileName, szResultEnding);
}

/*------------------------------------------------------------------------*/
CU_ErrorCode CU_list_tests_to_file()
{
  /* if a filename root hasn't been set, use the default one */
  if (0 == strlen(f_szTestListFileName)) {
    CU_set_output_filename(f_szDefaultFileRoot);
  }

  return automated_list_all_tests(CU_get_registry(), f_szTestListFileName);
}

/*=================================================================
 *  Static function implementation
 *=================================================================*/
/** Runs the registered tests using the automated interface.
 *  If non-NULL. the specified registry is set as the active
 *  registry for running the tests.  If NULL, then the default
 *  BCUnit test registry is used.  The actual test running is
 *  performed by CU_run_all_tests().
 *  @param pRegistry The test registry to run.
 */
static void automated_run_all_tests(CU_pTestRegistry pRegistry)
{
  CU_pTestRegistry pOldRegistry = NULL;

  assert(NULL != f_pTestResultFile);

  f_pRunningSuite = NULL;

  if (NULL != pRegistry) {
    pOldRegistry = CU_set_registry(pRegistry);
  }
  if (bJUnitXmlOutput == CU_FALSE) {
    fprintf(f_pTestResultFile, "  <BCUNIT_RESULT_LISTING> \n");
  }
  CU_run_all_tests();
  if (NULL != pRegistry) {
    CU_set_registry(pOldRegistry);
  }
}

/*------------------------------------------------------------------------*/
/** Initializes the test results file generated by the automated interface.
 *  A file stream is opened and header information is written.
 */
static CU_ErrorCode initialize_result_file(const char* szFilename)
{
  CU_set_error(CUE_SUCCESS);

  if ((NULL == szFilename) || (strlen(szFilename) == 0)) {
    CU_set_error(CUE_BAD_FILENAME);
  }
  else if (NULL == (f_pTestResultFile = fopen(szFilename, "w"))) {
    CU_set_error(CUE_FOPEN_FAILED);
  }
  else {
    setvbuf(f_pTestResultFile, NULL, _IONBF, 0);

    if (bJUnitXmlOutput == CU_TRUE) {
	    if (bPartialSuiteJUnitReport == CU_FALSE) {
		    fprintf(f_pTestResultFile,
			    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			    "<testsuites> \n");
	    }
    } else {
      fprintf(f_pTestResultFile,
              "<?xml version=\"1.0\" ?> \n"
              "<?xml-stylesheet type=\"text/xsl\" href=\"BCUnit-Run.xsl\" ?> \n"
              "<!DOCTYPE BCUNIT_TEST_RUN_REPORT SYSTEM \"BCUnit-Run.dtd\"> \n"
              "<BCUNIT_TEST_RUN_REPORT> \n"
              "  <BCUNIT_HEADER/> \n");
    }
  }

  return CU_get_error();
}

/*------------------------------------------------------------------------*/
/** Makes a copy of the source string with XML control characters
 *  translated to escape sequences.
 *  Potential optimization: Allocate only if there's something to translate,
 *  and tell caller if it should free or not.
 *  @param szSource  The source string that may contain XML control chars.
 *  @return a newly allocated string that is XML-safe. Callers MUST CU_FREE() it.
 */
static char* to_xml_safe(const char* szSource) {
  size_t szXmlSafe_len = CU_translated_strlen(szSource) + 1;
  char* szXmlSafe = (char *)CU_MALLOC(szXmlSafe_len);
  memset(szXmlSafe, 0, szXmlSafe_len);
  CU_translate_special_characters(szSource, szXmlSafe, szXmlSafe_len); // ignored return
  return szXmlSafe;
}

/*------------------------------------------------------------------------*/
/** Handler function called at start of each test.
 *  The test result file must have been opened before this
 *  function is called (i.e. f_pTestResultFile non-NULL).
 *  @param pTest  The test being run (non-NULL).
 *  @param pSuite The suite containing the test (non-NULL).
 */
static void automated_test_start_message_handler(const CU_pTest pTest, const CU_pSuite pSuite)
{
	char *szTempName = NULL;
	size_t szTempName_len = 0;

  //Gathering information about current time for the suite start time
  time_t suiteStartTime = time(NULL);
  struct tm tm = *localtime(&suiteStartTime);

  //Getting the start time of the test to compute test duration
  startTime = CU_get_wall_time();

  if( test_start_handler ){
    (*test_start_handler)(pTest, pSuite);
  }

  CU_UNREFERENCED_PARAMETER(pTest);   /* not currently used */

  assert(NULL != pTest);
  assert(NULL != pSuite);
  assert(NULL != pSuite->pName);
  assert(NULL != f_pTestResultFile);

  /* write suite close/open tags if this is the 1st test for this szSuite */
  if ((NULL == f_pRunningSuite) || (f_pRunningSuite != pSuite)) {
    if (CU_TRUE == f_bWriting_BCUNIT_RUN_SUITE) {
      if (bJUnitXmlOutput == CU_TRUE) {
        fprintf(f_pTestResultFile,
                "    </testsuite>\n");
      }
      else {
        fprintf(f_pTestResultFile,
                "      </BCUNIT_RUN_SUITE_SUCCESS> \n"
                "    </BCUNIT_RUN_SUITE> \n");
      }
    }

  /* translate suite name that may contain XML control characters */
  szTempName = (char *)CU_MALLOC((szTempName_len = CU_translated_strlen(pSuite->pName) + 1));
  CU_translate_special_characters(pSuite->pName, szTempName, szTempName_len);

    if (bJUnitXmlOutput == CU_TRUE) {
      fprintf(f_pTestResultFile,
              "  <testsuite name=\"%s\" tests=\"%u\" time=\"0\" failures=\"%u\" errors=\"%u\" skipped=\"0\" timestamp=\"%d-%02d-%02dT%02d:%02d:%02d\"> \n",
              (NULL != szTempName) ? szTempName : "", /* Name */
              pSuite->uiNumberOfTests, /* Tests */
              pSuite->uiNumberOfTestsFailed, /* Tests failure */
              0,
              tm.tm_year + 1900,
              tm.tm_mon + 1,
              tm.tm_mday,
              tm.tm_hour,
              tm.tm_min,
              tm.tm_sec
              ); /* Errors */
    } else {
      fprintf(f_pTestResultFile,
              "    <BCUNIT_RUN_SUITE> \n"
              "      <BCUNIT_RUN_SUITE_SUCCESS> \n"
              "        <SUITE_NAME> %s </SUITE_NAME> \n",
              (NULL != szTempName ? szTempName : ""));
    }

    f_bWriting_BCUNIT_RUN_SUITE = CU_TRUE;
    f_pRunningSuite = pSuite;
  }

  if (NULL != szTempName) {
    CU_FREE(szTempName);
  }
}

/*------------------------------------------------------------------------*/
/** Handler function called at completion of each test.
 * @param pTest   The test being run (non-NULL).
 * @param pSuite  The suite containing the test (non-NULL).
 * @param pFailure Pointer to the 1st failure record for this test.
 */
static void automated_test_complete_message_handler(const CU_pTest pTest,
                                                    const CU_pSuite pSuite,
                                                    const CU_pFailureRecord pFailure)
{
  char *szTemp = NULL;
  size_t szTemp_len = 0;
  size_t cur_len = 0;
  CU_pFailureRecord pTempFailure = pFailure;
  const char *pPackageName = CU_automated_package_name_get();

  //Getting the end time of the test to compute test duration
  endTime = CU_get_wall_time();

  if( test_complete_handler ){
    (*test_complete_handler)(pTest, pSuite, pFailure);
  }

  CU_UNREFERENCED_PARAMETER(pSuite);  /* pSuite is not used except in assertion */

  assert(NULL != pTest);
  assert(NULL != pTest->pName);
  assert(NULL != pSuite);
  assert(NULL != pSuite->pName);
  assert(NULL != f_pTestResultFile);

  /* translate test name that may contain XML control characters */
  char* szTestName = to_xml_safe(pTest->pName);

  if (NULL != pTempFailure) {

    if(NULL != pTempFailure) {
      if (bJUnitXmlOutput == CU_TRUE) {
        assert((NULL != pTempFailure->pSuite) && (pTempFailure->pSuite == pSuite));
        assert((NULL != pTempFailure->pTest) && (pTempFailure->pTest == pTest));

        if (NULL != pTempFailure->strCondition) {
          szTemp_len = CU_translated_strlen(pTempFailure->strCondition) + 1;
          szTemp = (char *)CU_MALLOC(szTemp_len);
          size_t test = CU_translate_special_characters(pTempFailure->strCondition, szTemp, szTemp_len);
        }
        else {
          //!!!!!!
          szTemp[0] = '\0';
        }

        fprintf(f_pTestResultFile, "        <testcase classname=\"%s.%s\" name=\"%s\" time=\"%f\">\n",
                pPackageName,
                pSuite->pName,
                szTestName,
                endTime - startTime
                );

        if(NULL != pTempFailure->pNext) {
          fprintf(f_pTestResultFile, "            <failure message=\"Multiple asserts failed ...\" type=\"Failure\">\n");
        }
        else {
          fprintf(f_pTestResultFile, "            <failure message=\"%s\" type=\"Failure\">\n", szTemp);
        }


      } /* if */
    }

    while (NULL != pTempFailure) {

      assert((NULL != pTempFailure->pSuite) && (pTempFailure->pSuite == pSuite));
      assert((NULL != pTempFailure->pTest) && (pTempFailure->pTest == pTest));

      /* expand temporary char buffer if need more room */
      if (NULL != pTempFailure->strCondition) {
        cur_len = CU_translated_strlen(pTempFailure->strCondition) + 1;
      }
      else {
        cur_len = 1;
      }
      if (cur_len > szTemp_len) {
        szTemp_len = cur_len;
        if (NULL != szTemp) {
          CU_FREE(szTemp);
        }
        szTemp = (char *)CU_MALLOC(szTemp_len);
      }

      /* convert xml entities in strCondition (if present) */
      if (NULL != pTempFailure->strCondition) {
        CU_translate_special_characters(pTempFailure->strCondition, szTemp, szTemp_len);
      }
      else {
        szTemp[0] = '\0';
      }

      if (bJUnitXmlOutput == CU_TRUE) {
        fprintf(f_pTestResultFile, "                     Condition: %s\n", szTemp);
        fprintf(f_pTestResultFile, "                     File     : %s\n", (NULL != pTempFailure->strFileName) ? pTempFailure->strFileName : "");
        fprintf(f_pTestResultFile, "                     Line     : %u\n", pTempFailure->uiLineNumber);
      } else {
        fprintf(f_pTestResultFile,
              "        <BCUNIT_RUN_TEST_RECORD> \n"
              "          <BCUNIT_RUN_TEST_FAILURE> \n"
              "            <TEST_NAME> %s </TEST_NAME> \n"
              "            <FILE_NAME> %s </FILE_NAME> \n"
              "            <LINE_NUMBER> %u </LINE_NUMBER> \n"
              "            <CONDITION> %s </CONDITION> \n"
              "          </BCUNIT_RUN_TEST_FAILURE> \n"
              "        </BCUNIT_RUN_TEST_RECORD> \n",
              szTestName,
              (NULL != pTempFailure->strFileName) ? pTempFailure->strFileName : "",
              pTempFailure->uiLineNumber,
              szTemp);
      } /* if */
      pTempFailure = pTempFailure->pNext;
    } /* while */

    if (bJUnitXmlOutput == CU_TRUE) {
      fprintf(f_pTestResultFile, "            </failure>\n");
      fprintf(f_pTestResultFile, "        </testcase>\n");
    } /* if */
  }
  else {
    if (bJUnitXmlOutput == CU_TRUE) {
      fprintf(f_pTestResultFile,  "        <testcase classname=\"%s.%s\" name=\"%s\" time=\"%f\"/>\n",
              pPackageName,
              pSuite->pName,
              szTestName,
              endTime - startTime
              );
    } else {
      fprintf(f_pTestResultFile,
              "        <BCUNIT_RUN_TEST_RECORD> \n"
              "          <BCUNIT_RUN_TEST_SUCCESS> \n"
              "            <TEST_NAME> %s </TEST_NAME> \n"
              "          </BCUNIT_RUN_TEST_SUCCESS> \n"
              "        </BCUNIT_RUN_TEST_RECORD> \n",
              szTestName);
    }
  }

  CU_FREE(szTestName);
  if (NULL != szTemp) {
    CU_FREE(szTemp);
  }
}

/*------------------------------------------------------------------------*/
/** Handler function called at completion of all tests in a suite.
 *  @param pFailure Pointer to the test failure record list.
 */
static void automated_all_tests_complete_message_handler(const CU_pFailureRecord pFailure)
{
  CU_pTestRegistry pRegistry = CU_get_registry();
  CU_pRunSummary pRunSummary = CU_get_run_summary();

  if( all_test_complete_handler ){
    (*all_test_complete_handler)(pFailure);
  }

  CU_UNREFERENCED_PARAMETER(pFailure);  /* not used */

  assert(NULL != pRegistry);
  assert(NULL != pRunSummary);
  assert(NULL != f_pTestResultFile);

  if ((NULL != f_pRunningSuite) && (CU_TRUE == f_bWriting_BCUNIT_RUN_SUITE)) {
    if (bJUnitXmlOutput == CU_FALSE) {
      fprintf(f_pTestResultFile,
              "      </BCUNIT_RUN_SUITE_SUCCESS> \n"
              "    </BCUNIT_RUN_SUITE> \n");
    }
  }

  if (bJUnitXmlOutput == CU_FALSE) {
    fprintf(f_pTestResultFile,
            "  </BCUNIT_RESULT_LISTING>\n"
            "  <BCUNIT_RUN_SUMMARY> \n");

    fprintf(f_pTestResultFile,
            "    <BCUNIT_RUN_SUMMARY_RECORD> \n"
            "      <TYPE> %s </TYPE> \n"
            "      <TOTAL> %u </TOTAL> \n"
            "      <RUN> %u </RUN> \n"
            "      <SUCCEEDED> - NA - </SUCCEEDED> \n"
            "      <FAILED> %u </FAILED> \n"
            "      <INACTIVE> %u </INACTIVE> \n"
            "    </BCUNIT_RUN_SUMMARY_RECORD> \n",
            _("Suites"),
            pRegistry->uiNumberOfSuites,
            pRunSummary->nSuitesRun,
            pRunSummary->nSuitesFailed,
            pRunSummary->nSuitesInactive);

    fprintf(f_pTestResultFile,
            "    <BCUNIT_RUN_SUMMARY_RECORD> \n"
            "      <TYPE> %s </TYPE> \n"
            "      <TOTAL> %u </TOTAL> \n"
            "      <RUN> %u </RUN> \n"
            "      <SUCCEEDED> %u </SUCCEEDED> \n"
            "      <FAILED> %u </FAILED> \n"
            "      <INACTIVE> %u </INACTIVE> \n"
            "    </BCUNIT_RUN_SUMMARY_RECORD> \n",
            _("Test Cases"),
            pRegistry->uiNumberOfTests,
            pRunSummary->nTestsRun,
            pRunSummary->nTestsRun - pRunSummary->nTestsFailed,
            pRunSummary->nTestsFailed,
            pRunSummary->nTestsInactive);

    fprintf(f_pTestResultFile,
            "    <BCUNIT_RUN_SUMMARY_RECORD> \n"
            "      <TYPE> %s </TYPE> \n"
            "      <TOTAL> %u </TOTAL> \n"
            "      <RUN> %u </RUN> \n"
            "      <SUCCEEDED> %u </SUCCEEDED> \n"
            "      <FAILED> %u </FAILED> \n"
            "      <INACTIVE> %s </INACTIVE> \n"
            "    </BCUNIT_RUN_SUMMARY_RECORD> \n"
            "  </BCUNIT_RUN_SUMMARY> \n",
            _("Assertions"),
            pRunSummary->nAsserts,
            pRunSummary->nAsserts,
            pRunSummary->nAsserts - pRunSummary->nAssertsFailed,
            pRunSummary->nAssertsFailed,
            _("n/a"));
    }
}

/*------------------------------------------------------------------------*/
/** Handler function called when suite initialization fails.
 *  @param pSuite The suite for which initialization failed.
 */
static void automated_suite_init_failure_message_handler(const CU_pSuite pSuite)
{

  if( suite_init_failure_handler ){
    (*suite_init_failure_handler)(pSuite);
  }

  assert(NULL != pSuite);
  assert(NULL != pSuite->pName);
  assert(NULL != f_pTestResultFile);

  if (CU_TRUE == f_bWriting_BCUNIT_RUN_SUITE) {
    if (bJUnitXmlOutput == CU_TRUE) {
      f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;
      fprintf(f_pTestResultFile,
              "    </testsuite>\n");
    } else {
      fprintf(f_pTestResultFile,
              "      </BCUNIT_RUN_SUITE_SUCCESS> \n"
              "    </BCUNIT_RUN_SUITE> \n");
      f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;
    }
  }

  if (bJUnitXmlOutput == CU_FALSE) {
    fprintf(f_pTestResultFile,
            "    <BCUNIT_RUN_SUITE> \n"
            "      <BCUNIT_RUN_SUITE_FAILURE> \n"
            "        <SUITE_NAME> %s </SUITE_NAME> \n"
            "        <FAILURE_REASON> %s </FAILURE_REASON> \n"
            "      </BCUNIT_RUN_SUITE_FAILURE> \n"
            "    </BCUNIT_RUN_SUITE>  \n",
            pSuite->pName,
            _("Suite Initialization Failed"));
  }
}

/*------------------------------------------------------------------------*/
/** Handler function called when suite cleanup fails.
 *  @param pSuite The suite for which cleanup failed.
 */
static void automated_suite_cleanup_failure_message_handler(const CU_pSuite pSuite)
{

  if( suite_cleanup_failure_handler ){
    (*suite_cleanup_failure_handler)(pSuite);
  }

  assert(NULL != pSuite);
  assert(NULL != pSuite->pName);
  assert(NULL != f_pTestResultFile);

  if (CU_TRUE == f_bWriting_BCUNIT_RUN_SUITE) {
    if (bJUnitXmlOutput == CU_TRUE) {
      f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;
      fprintf(f_pTestResultFile,
              "    </testsuite>\n");
    } else {
      fprintf(f_pTestResultFile,
              "      </BCUNIT_RUN_SUITE_SUCCESS> \n"
              "    </BCUNIT_RUN_SUITE> \n");
      f_bWriting_BCUNIT_RUN_SUITE = CU_FALSE;
    }
  }

  if (bJUnitXmlOutput == CU_TRUE) {
    fprintf(f_pTestResultFile,
            "    <testsuite name=\"Suite Cleanup\"> \n"
            "        <testcase name=\"%s\" result=\"failure\"> \n"
            "            <error> \"Cleanup of suite failed.\" </error> \n"
            "          <variation name=\"error\"> \n"
            "            <severity>fail</severity> \n"
            "            <description> \"Cleanup of suite failed.\" </description> \n"
            "            <resource> SuiteCleanup </resource> \n"
            "          </variation> \n"
            "       </testcase> \n"
            "    </testsuite>\n",
            (NULL != pSuite->pName) ? pSuite->pName : "");
  } else {
    fprintf(f_pTestResultFile,
            "    <BCUNIT_RUN_SUITE> \n"
            "      <BCUNIT_RUN_SUITE_FAILURE> \n"
            "        <SUITE_NAME> %s </SUITE_NAME> \n"
            "        <FAILURE_REASON> %s </FAILURE_REASON> \n"
            "      </BCUNIT_RUN_SUITE_FAILURE> \n"
            "    </BCUNIT_RUN_SUITE>  \n",
            pSuite->pName,
            _("Suite Cleanup Failed"));
  }
}

/*------------------------------------------------------------------------*/
/** Finalizes and closes the results output file generated
 *  by the automated interface.
 */
static CU_ErrorCode uninitialize_result_file(void)
{
  char* szTime;
  time_t tTime = 0;

  assert(NULL != f_pTestResultFile);

  CU_set_error(CUE_SUCCESS);

  time(&tTime);
  szTime = ctime(&tTime);
  if (szTime) szTime[24] = '\0';
  if (bJUnitXmlOutput == CU_TRUE) {
	  fprintf(f_pTestResultFile,
		  "    </testsuite>\n");
	  if (bPartialSuiteJUnitReport == CU_FALSE) {
		  fprintf(f_pTestResultFile, "</testsuites>\n");
	  }
  }
  else {
	  fprintf(f_pTestResultFile,
		  "  <BCUNIT_FOOTER> %s" CU_VERSION " - %s </BCUNIT_FOOTER> \n"
		  "</BCUNIT_TEST_RUN_REPORT>\n",
		  _("File Generated By BCUnit v"),
		  (NULL != szTime) ? szTime : "");
  }

  if (0 != fclose(f_pTestResultFile)) {
    CU_set_error(CUE_FCLOSE_FAILED);
  }

  return CU_get_error();
}

/*------------------------------------------------------------------------*/
/** Generates an xml listing of all tests in all suites for the
 *  specified test registry.  The output is directed to a file
 *  having the specified name.
 *  @param pRegistry   Test registry for which to generate list (non-NULL).
 *  @param szFilename  Non-NULL, non-empty string containing name for
 *                     listing file.
 *  @return  A CU_ErrorCode indicating the error status.
 */
static CU_ErrorCode automated_list_all_tests(CU_pTestRegistry pRegistry, const char* szFilename)
{
  CU_pSuite pSuite = NULL;
  CU_pTest  pTest = NULL;
  FILE* pTestListFile = NULL;
  char* szTime;
  time_t tTime = 0;

  CU_set_error(CUE_SUCCESS);

  if (NULL == pRegistry) {
    CU_set_error(CUE_NOREGISTRY);
  }
  else if ((NULL == szFilename) || (0 == strlen(szFilename))) {
    CU_set_error(CUE_BAD_FILENAME);
  }
  else if (NULL == (pTestListFile = fopen(f_szTestListFileName, "w"))) {
    CU_set_error(CUE_FOPEN_FAILED);
  }
  else {
    setvbuf(pTestListFile, NULL, _IONBF, 0);

    fprintf(pTestListFile,
            "<?xml version=\"1.0\" ?> \n"
            "<?xml-stylesheet type=\"text/xsl\" href=\"BCUnit-List.xsl\" ?> \n"
            "<!DOCTYPE BCUNIT_TEST_LIST_REPORT SYSTEM \"BCUnit-List.dtd\"> \n"
            "<BCUNIT_TEST_LIST_REPORT> \n"
            "  <BCUNIT_HEADER/> \n"
            "  <BCUNIT_LIST_TOTAL_SUMMARY> \n");

    fprintf(pTestListFile,
            "    <BCUNIT_LIST_TOTAL_SUMMARY_RECORD> \n"
            "      <BCUNIT_LIST_TOTAL_SUMMARY_RECORD_TEXT> %s </BCUNIT_LIST_TOTAL_SUMMARY_RECORD_TEXT> \n"
            "      <BCUNIT_LIST_TOTAL_SUMMARY_RECORD_VALUE> %u </BCUNIT_LIST_TOTAL_SUMMARY_RECORD_VALUE> \n"
            "    </BCUNIT_LIST_TOTAL_SUMMARY_RECORD> \n",
            _("Total Number of Suites"),
            pRegistry->uiNumberOfSuites);

    fprintf(pTestListFile,
            "    <BCUNIT_LIST_TOTAL_SUMMARY_RECORD> \n"
            "      <BCUNIT_LIST_TOTAL_SUMMARY_RECORD_TEXT> %s </BCUNIT_LIST_TOTAL_SUMMARY_RECORD_TEXT> \n"
            "      <BCUNIT_LIST_TOTAL_SUMMARY_RECORD_VALUE> %u </BCUNIT_LIST_TOTAL_SUMMARY_RECORD_VALUE> \n"
            "    </BCUNIT_LIST_TOTAL_SUMMARY_RECORD> \n"
            "  </BCUNIT_LIST_TOTAL_SUMMARY> \n",
            _("Total Number of Test Cases"),
            pRegistry->uiNumberOfTests);

    fprintf(pTestListFile,
            "  <BCUNIT_ALL_TEST_LISTING> \n");

    pSuite = pRegistry->pSuite;
    while (NULL != pSuite) {
      assert(NULL != pSuite->pName);
      pTest = pSuite->pTest;

      fprintf(pTestListFile,
              "    <BCUNIT_ALL_TEST_LISTING_SUITE> \n"
              "      <BCUNIT_ALL_TEST_LISTING_SUITE_DEFINITION> \n"
              "        <SUITE_NAME> %s </SUITE_NAME> \n"
              "        <INITIALIZE_VALUE> %s </INITIALIZE_VALUE> \n"
              "        <CLEANUP_VALUE> %s </CLEANUP_VALUE> \n"
              "        <ACTIVE_VALUE> %s </ACTIVE_VALUE> \n"
              "        <TEST_COUNT_VALUE> %u </TEST_COUNT_VALUE> \n"
              "      </BCUNIT_ALL_TEST_LISTING_SUITE_DEFINITION> \n",
               pSuite->pName,
              (NULL != pSuite->pInitializeFunc) ? _("Yes") : _("No"),
              (NULL != pSuite->pCleanupFunc) ? _("Yes") : _("No"),
              (CU_FALSE != pSuite->fActive) ? _("Yes") : _("No"),
              pSuite->uiNumberOfTests);

      fprintf(pTestListFile,
              "      <BCUNIT_ALL_TEST_LISTING_SUITE_TESTS> \n");
      while (NULL != pTest) {
        assert(NULL != pTest->pName);
        /* translate test name that may contain XML control characters */
        char* szTestName = to_xml_safe(pTest->pName);
        fprintf(pTestListFile,
                "        <TEST_CASE_DEFINITION> \n"
                "          <TEST_CASE_NAME> %s </TEST_CASE_NAME> \n"
                "          <TEST_ACTIVE_VALUE> %s </TEST_ACTIVE_VALUE> \n"
                "        </TEST_CASE_DEFINITION> \n",
                szTestName,
                (CU_FALSE != pSuite->fActive) ? _("Yes") : _("No"));
        CU_FREE(szTestName);
        pTest = pTest->pNext;
      }

      fprintf(pTestListFile,
              "      </BCUNIT_ALL_TEST_LISTING_SUITE_TESTS> \n"
              "    </BCUNIT_ALL_TEST_LISTING_SUITE> \n");

      pSuite = pSuite->pNext;
    }

    fprintf(pTestListFile, "  </BCUNIT_ALL_TEST_LISTING> \n");

    time(&tTime);
    szTime = ctime(&tTime);
    if (szTime) szTime[24] = '\0';
    fprintf(pTestListFile,
            "  <BCUNIT_FOOTER> %s" CU_VERSION " - %s </BCUNIT_FOOTER> \n"
            "</BCUNIT_TEST_LIST_REPORT>\n",
            _("File Generated By BCUnit v"),
            (NULL != szTime) ? szTime : "");

    if (0 != fclose(pTestListFile)) {
      CU_set_error(CUE_FCLOSE_FAILED);
    }
  }

  return CU_get_error();
}

/*------------------------------------------------------------------------*/
/** Enable or Disable the XML output format to JUnit-like. When enabled (CU_TRUE)
 *  then the Results xml that is produced can be read by cruisecontrol and displayed
 *  in the test results page.
 */
void CU_automated_enable_junit_xml(CU_BOOL bFlag)
{
  bJUnitXmlOutput = bFlag;
}
/** @} */

/*------------------------------------------------------------------------*/
/** Enable or Disable the englobing XML tags for <testsuites> in the JUnit file report
 *  This allows to create partial JUnit results file that will need to be merged.
 */
void CU_automated_enable_partial_junit(CU_BOOL bFlag)
{
  bPartialSuiteJUnitReport = bFlag;
}

/*------------------------------------------------------------------------*/
/** Set tests suites package name
 */
void CU_automated_package_name_set(const char *pName)
{
  memset(_gPackageName, 0, sizeof(_gPackageName));

  /* Is object valid? */
  if (pName) {
    strncpy(_gPackageName, pName, sizeof(_gPackageName) - 1);
    _gPackageName[sizeof(_gPackageName) - 1] = '\0';
  }
}

/*------------------------------------------------------------------------*/
/** Get tests suites package name
 */
const char *CU_automated_package_name_get()
{
 return _gPackageName;
}
/** @} */
