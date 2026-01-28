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

#include <stdlib.h>                       /* getenv                          */
#include <string.h>                       /* strcpy, strcmp                  */

#include "parseArgs/baStd.h"              /* BA standard header file         */
extern "C"
{
#include "ktrace/kTrace.h"
}
#include "orionld/common/traceLevels.h"

#include "parseArgs/paPrivate.h"          /* PaTypeUnion, config variables,  */
#include "parseArgs/paTraceLevels.h"      /* LmtPaEnvVal, ...                */
#include "parseArgs/parseArgs.h"          /* PaArgument                      */
#include "parseArgs/paBuiltin.h"          /* paBuiltin, paBuiltinNoOf        */
#include "parseArgs/paWarning.h"          /* paWaringInit, paWarningAdd      */
#include "parseArgs/paIterate.h"          /* paIterateInit, paIterateNext    */
#include "parseArgs/paConfig.h"           /* paConfigActions                 */
#include "parseArgs/paEnvVals.h"          /* Own interface                   */



/* ****************************************************************************
*
* builtins - 
*/
extern int builtins;



/* ****************************************************************************
*
* paEnvName - get real name of variable (environment or RC-file variable)
*/
char* paEnvName(PaiArgument* aP, char* out, int outLen)
{
  bool isbuiltin = ((aP->what & PawBuiltin) == PawBuiltin);

  if (aP->envName == NULL)
  {
    out[0] = 0;
  }
  else if (aP->envName[0] == 0)
  {
    out[0] = 0;
  }
  else if (aP->envName[0] == '!')
  {
    snprintf(out, outLen, "%s", &aP->envName[1]);
  }
  else if (isbuiltin && (paBuiltinPrefix != NULL) && (paBuiltinPrefix[0] != 0))
  {
    snprintf(out, outLen, "%s%s", paBuiltinPrefix, aP->envName);
  }
  else if (!isbuiltin && (paPrefix != NULL) && (paPrefix[0] != 0))
  {
    snprintf(out, outLen, "%s%s", paPrefix, aP->envName);
  }
  else
  {
    snprintf(out, outLen, "%s", aP->envName);
  }

  return out;
}



/* ****************************************************************************
*
* paEnvVals - 
*/
int paEnvVals(PaiArgument* paList)
{
  PaiArgument*  aP;
  char         w[512];

  paIterateInit();
  while ((aP = paIterateNext(paList)) != NULL)
  {
    char* val;
    char  envVarName[80];

    KT_T(KtPaEnvVal, "got aP '%s'", aP->name);

    if ((aP->what & PawVariable) == 0)
    {
      KT_T(KtPaEnvVal, "skipping aP '%s' as it is no variable", aP->name);
      continue;
    }

    paEnvName(aP, envVarName, sizeof(envVarName));

    KT_T(KtPaEnvVal, "looking for '%s'", envVarName);

    val = getenv(envVarName);
    if (val)
    {
      aP->from = PafEnvVar;
      KT_T(KtPaEnvVal, "got value '%s' for %s", val, envVarName);

      switch (aP->type)
      {
      case PaString:
        strcpy((char*) aP->varP, val);
        break;

      case PaBoolean:
        if (strcmp(val, "TRUE") == 0)
          *((bool*) (int64_t) aP->varP) = true;
        else if (strcmp(val, "FALSE") == 0)
          *((bool*) (int64_t) aP->varP) = false;
        else
        {
          snprintf(w, sizeof(w), "Bad Value '%s' for boolean variable '%s' - only 'TRUE' and 'FALSE' allowed as value for booleans", val, envVarName);
          PA_WARNING(PasNoSuchBooleanValue, w);
        }
        break;

      case PaSList:
        break;

      case PaIList:
        break;

      case PaInt:
      case PaIntU:
        *((int*) aP->varP) = baStoi(val);
        KT_T(KtPaEnvVal, "got value %d for %s", *((int*) aP->varP), envVarName);
        break;

      case PaInt64:
      case PaIntU64:
        *((int64_t*) aP->varP) = baStoi(val);
        KT_T(KtPaEnvVal, "got value %d for %s", *((int*) aP->varP), envVarName);
        break;

      case PaShort:
      case PaShortU:
        *((int16_t*) (int64_t) aP->varP) = baStoi(val);
        KT_T(KtPaEnvVal, "got value %d for %s", *((int16_t*) aP->varP), envVarName);
        break;

      case PaFloat:
        *((float*) (int64_t) aP->varP) = baStof(val);
        KT_T(KtPaEnvVal, "got value %f for %s", *((float*) aP->varP), envVarName);
        break;

      case PaDouble:
        *((double*) (int64_t) aP->varP) = baStod(val);
        KT_T(KtPaEnvVal, "got value %f for %s", *((double*) aP->varP), envVarName);
        break;

      case PaChar:
      case PaCharU:
        *((char*) (int64_t) aP->varP) = baStoi(val);
        KT_T(KtPaEnvVal, "got value %d for %s", *((char*) aP->varP), envVarName);
        break;

      default:
        snprintf(w, sizeof(w), "bad type %d for variable %s", aP->type, envVarName);
        PA_WARNING(PasNoSuchType, w);
        return -1;
      }
    }
  }

  return 0;
}
