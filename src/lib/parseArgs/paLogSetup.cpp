/*
*
* Copyright 2013 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: developer
*/
#include <stdio.h>                    /* stderr, stdout, ...                 */
#include <cstdlib>                    /* C++ free(.)                         */

#include "parseArgs/baStd.h"          /* BA standard header file             */
extern "C"
{
#include "ktrace/kTrace.h"
}

#include "parseArgs/paPrivate.h"      /* PaTypeUnion, config variables, ...  */
#include "parseArgs/paBuiltin.h"      /* paLogDir                            */
#include "parseArgs/paTraceLevels.h"  /* KtPaEnvVal, ...                     */
#include "parseArgs/paConfig.h"       /* paConfigActions                     */
#include "parseArgs/paWarning.h"      /* paWaringInit, paWarningAdd          */
#include "parseArgs/paLogSetup.h"     /* Own interface                       */



/* ****************************************************************************
*
* File descriptors for logging (kept for compatibility)
*/
int  lmFd   = -1;
int  lmSd   = -1;



/* ****************************************************************************
*
* paLmFdGet -
*/
int paLmFdGet(void)
{
  return lmFd;
}



/* ****************************************************************************
*
* paLmSdGet
*/
int paLmSdGet(void)
{
  return lmSd;
}



/* ****************************************************************************
*
* paLogSetup - initialize ktrace logging
*/
extern char* paExtraLogSuffix;
int paLogSetup(void)
{
  // Initialize ktrace
  const char* logDir = (paLogToFile && paLogDir[0] != 0) ? paLogDir : NULL;

  ktInit(paProgName,         // progName
         logDir,             // logDir (NULL if not logging to file)
         paLogToScreen,      // logToScreen
         paLogLevel,         // logLevel
         paTraceV,           // traceLevels
         paVerbose,          // verbose
         paDebug,            // debug
         false);             // fixme

  return 0;
}
