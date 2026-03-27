/*
*
* Copyright 2025 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Carsten Frey
*/
#include <string.h>                                              // strcmp, strlen, strchr
#include <stdlib.h>                                              // strtod, atoi
#include <math.h>                                                // sqrt
#include <stdio.h>                                               // snprintf

extern "C"
{
#include "ktrace/kTrace.h"                                       // KT_*
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBuilder.h"                                     // kjArray, kjString, kjFloat, kjObject, kjChildAdd
}

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/aggregatedValuesTransform.h"            // Own interface



// -----------------------------------------------------------------------------
//
// AggrMethod -
//
typedef enum AggrMethod
{
  AggrAvg,
  AggrMin,
  AggrMax,
  AggrSum,
  AggrSumSq,
  AggrStddev,
  AggrDistinctCount
} AggrMethod;

#define MAX_AGGR_METHODS 7



// -----------------------------------------------------------------------------
//
// parseAggrMethods - parse comma-separated aggregation method names
//
// Returns the number of methods parsed, fills methodsOut array.
//
static int parseAggrMethods(const char* aggrMethods, AggrMethod* methodsOut, int maxMethods)
{
  int   count = 0;
  char  buf[256];

  strncpy(buf, aggrMethods, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  char* token = buf;
  while (token != NULL && count < maxMethods)
  {
    char* comma = strchr(token, ',');
    if (comma != NULL)
      *comma = 0;

    // Trim whitespace
    while (*token == ' ') token++;

    if      (strcmp(token, "avg")           == 0) methodsOut[count++] = AggrAvg;
    else if (strcmp(token, "min")           == 0) methodsOut[count++] = AggrMin;
    else if (strcmp(token, "max")           == 0) methodsOut[count++] = AggrMax;
    else if (strcmp(token, "sum")           == 0) methodsOut[count++] = AggrSum;
    else if (strcmp(token, "sumsq")         == 0) methodsOut[count++] = AggrSumSq;
    else if (strcmp(token, "stddev")        == 0) methodsOut[count++] = AggrStddev;
    else if (strcmp(token, "distinctCount") == 0) methodsOut[count++] = AggrDistinctCount;
    else
      KT_W("Unknown aggregation method: '%s'", token);

    token = (comma != NULL) ? comma + 1 : NULL;
  }

  return count;
}



// -----------------------------------------------------------------------------
//
// aggrMethodName - return the string name for an AggrMethod
//
static const char* aggrMethodName(AggrMethod method)
{
  switch (method)
  {
  case AggrAvg:           return "avg";
  case AggrMin:           return "min";
  case AggrMax:           return "max";
  case AggrSum:           return "sum";
  case AggrSumSq:         return "sumsq";
  case AggrStddev:        return "stddev";
  case AggrDistinctCount: return "distinctCount";
  default:                return "unknown";
  }
}



// -----------------------------------------------------------------------------
//
// parseIso8601Duration - parse ISO 8601 duration to seconds
//
// Supports: P[nY][nM][nW][nD][T[nH][nM][nS]]
// Returns 0 if the duration is "PT0S", "P0D", etc. (meaning entire timespan)
//
static long parseIso8601Duration(const char* duration)
{
  if (duration == NULL || *duration == 0)
    return 0;

  if (*duration != 'P')
    return 0;

  const char* p = duration + 1;
  long  seconds = 0;
  bool  inTime  = false;

  while (*p != 0)
  {
    if (*p == 'T')
    {
      inTime = true;
      p++;
      continue;
    }

    // Parse the numeric value
    char* end = NULL;
    long  val = strtol(p, &end, 10);

    if (end == p)
    {
      p++;
      continue;
    }

    switch (*end)
    {
    case 'Y': seconds += val * 365 * 24 * 3600; break;
    case 'W': seconds += val * 7 * 24 * 3600;   break;
    case 'D': seconds += val * 24 * 3600;        break;
    case 'H': seconds += val * 3600;             break;
    case 'M':
      if (inTime)
        seconds += val * 60;
      else
        seconds += val * 30 * 24 * 3600;  // approximate month
      break;
    case 'S': seconds += val; break;
    default: break;
    }

    p = end + 1;
  }

  return seconds;
}



// -----------------------------------------------------------------------------
//
// parseIso8601ToEpoch - parse an ISO 8601 datetime string to epoch seconds
//
// Simple parser for "YYYY-MM-DDThh:mm:ssZ" format
//
static double parseIso8601ToEpoch(const char* datetime)
{
  if (datetime == NULL)
    return 0;

  struct tm tmVal;
  memset(&tmVal, 0, sizeof(tmVal));

  int matched = sscanf(datetime, "%d-%d-%dT%d:%d:%d",
                       &tmVal.tm_year, &tmVal.tm_mon, &tmVal.tm_mday,
                       &tmVal.tm_hour, &tmVal.tm_min, &tmVal.tm_sec);

  if (matched < 3)
    return 0;

  tmVal.tm_year -= 1900;
  tmVal.tm_mon  -= 1;

  return (double) timegm(&tmVal);
}



// -----------------------------------------------------------------------------
//
// epochToIso8601 - convert epoch seconds to ISO 8601 datetime string
//
static void epochToIso8601(double epoch, char* buf, int bufSize)
{
  time_t t = (time_t) epoch;
  struct tm tmVal;

  gmtime_r(&t, &tmVal);
  snprintf(buf, bufSize, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday,
           tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec);
}



// -----------------------------------------------------------------------------
//
// getNumericValue - extract a numeric value from an attribute instance
//
// Returns true if a numeric value was found, false otherwise.
//
static bool getNumericValue(KjNode* instanceP, double* valueOut)
{
  for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
  {
    if (strcmp(fieldP->name, "value") == 0)
    {
      if (fieldP->type == KjFloat)
      {
        *valueOut = fieldP->value.f;
        return true;
      }
      else if (fieldP->type == KjInt)
      {
        *valueOut = (double) fieldP->value.i;
        return true;
      }
      return false;
    }
  }
  return false;
}



// -----------------------------------------------------------------------------
//
// getObservedAt - extract observedAt from an attribute instance as epoch
//
static double getObservedAt(KjNode* instanceP)
{
  for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
  {
    if (strcmp(fieldP->name, "observedAt") == 0 && fieldP->type == KjString)
      return parseIso8601ToEpoch(fieldP->value.s);
  }
  return 0;
}



// -----------------------------------------------------------------------------
//
// Accumulator - tracks running aggregation state
//
typedef struct Accumulator
{
  double  sum;
  double  sumsq;
  double  min;
  double  max;
  int     count;
  int     distinctCount;
  double  distinctValues[1024];  // simple approach for distinctCount
} Accumulator;



// -----------------------------------------------------------------------------
//
// accumulatorInit -
//
static void accumulatorInit(Accumulator* accP)
{
  accP->sum           = 0;
  accP->sumsq         = 0;
  accP->min           = 1e308;
  accP->max           = -1e308;
  accP->count         = 0;
  accP->distinctCount = 0;
}



// -----------------------------------------------------------------------------
//
// accumulatorAdd -
//
static void accumulatorAdd(Accumulator* accP, double value)
{
  accP->sum   += value;
  accP->sumsq += value * value;

  if (value < accP->min) accP->min = value;
  if (value > accP->max) accP->max = value;

  // Track distinct values (simple O(n) approach, bounded by array size)
  bool found = false;
  for (int i = 0; i < accP->distinctCount && i < 1024; i++)
  {
    if (accP->distinctValues[i] == value)
    {
      found = true;
      break;
    }
  }
  if (!found && accP->distinctCount < 1024)
  {
    accP->distinctValues[accP->distinctCount] = value;
    accP->distinctCount++;
  }

  accP->count++;
}



// -----------------------------------------------------------------------------
//
// accumulatorResult - get the result for a specific aggregation method
//
static double accumulatorResult(Accumulator* accP, AggrMethod method)
{
  if (accP->count == 0)
    return 0;

  switch (method)
  {
  case AggrAvg:           return accP->sum / accP->count;
  case AggrMin:           return accP->min;
  case AggrMax:           return accP->max;
  case AggrSum:           return accP->sum;
  case AggrSumSq:         return accP->sumsq;
  case AggrStddev:
    {
      double mean     = accP->sum / accP->count;
      double variance = (accP->sumsq / accP->count) - (mean * mean);
      return (variance > 0) ? sqrt(variance) : 0;
    }
  case AggrDistinctCount: return (double) accP->distinctCount;
  default:                return 0;
  }
}



// -----------------------------------------------------------------------------
//
// aggregatedValuesTransform -
//
// Transform the temporal entity from normalized array format to aggregatedValues format.
// Each attribute array of instances becomes an object with aggregation method arrays.
//
// Input:  "P1": [ {"type":"Property", "value":30, "observedAt":"..."}, ... ]
// Output: "P1": { "type":"Property", "avg": [[100.0, "startTime", "endTime"]], "min": [...] }
//
// Each aggregation tuple is [value, periodStart, periodEnd].
// If aggrPeriodDuration is 0 or not specified, one period covers the entire timespan.
//
void aggregatedValuesTransform
(
  KjNode*      entityP,
  const char*  aggrMethods,
  const char*  aggrPeriodDuration,
  const char*  timeAt,
  const char*  endTimeAt
)
{
  // Parse aggregation methods
  AggrMethod methods[MAX_AGGR_METHODS];
  int        methodCount = parseAggrMethods(aggrMethods, methods, MAX_AGGR_METHODS);

  if (methodCount == 0)
    return;

  // Parse period duration
  long periodSeconds = parseIso8601Duration(aggrPeriodDuration);

  // Determine the overall time range from timeAt/endTimeAt
  double rangeStart = parseIso8601ToEpoch(timeAt);
  double rangeEnd   = (endTimeAt != NULL) ? parseIso8601ToEpoch(endTimeAt) : 0;

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    // Skip id, type, scope, and system attributes (non-array members)
    if (attrP->type != KjArray)
      continue;

    // Determine actual time range from instances if not provided by query params
    double actualStart = 1e18;
    double actualEnd   = 0;
    const char* attrType = NULL;

    for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
    {
      if (instanceP->type != KjObject)
        continue;

      if (attrType == NULL)
      {
        for (KjNode* fieldP = instanceP->value.firstChildP; fieldP != NULL; fieldP = fieldP->next)
        {
          if (strcmp(fieldP->name, "type") == 0)
          {
            attrType = fieldP->value.s;
            break;
          }
        }
      }

      double obsAt = getObservedAt(instanceP);
      if (obsAt > 0)
      {
        if (obsAt < actualStart) actualStart = obsAt;
        if (obsAt > actualEnd)   actualEnd   = obsAt;
      }
    }

    // Use query range if available, otherwise use actual data range
    double periodStart = (rangeStart > 0) ? rangeStart : actualStart;
    double periodEnd   = (rangeEnd > 0)   ? rangeEnd   : actualEnd;

    if (periodEnd <= periodStart)
      periodEnd = periodStart + 1;

    // Build aggregated values object
    KjNode* aggrObj = kjObject(orionldState.kjsonP, attrP->name);

    if (attrType != NULL)
      kjChildAdd(aggrObj, kjString(orionldState.kjsonP, "type", attrType));

    // For each aggregation method, create an array of [value, start, end] tuples
    for (int m = 0; m < methodCount; m++)
    {
      KjNode* methodArray = kjArray(orionldState.kjsonP, aggrMethodName(methods[m]));

      if (periodSeconds <= 0)
      {
        // Single period covering entire timespan
        Accumulator acc;
        accumulatorInit(&acc);

        for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
        {
          if (instanceP->type != KjObject)
            continue;

          double val;
          if (getNumericValue(instanceP, &val))
            accumulatorAdd(&acc, val);
        }

        if (acc.count > 0)
        {
          char startBuf[64], endBuf[64];
          epochToIso8601(periodStart, startBuf, sizeof(startBuf));
          epochToIso8601(periodEnd, endBuf, sizeof(endBuf));

          KjNode* tuple = kjArray(orionldState.kjsonP, NULL);
          kjChildAdd(tuple, kjFloat(orionldState.kjsonP, NULL, accumulatorResult(&acc, methods[m])));
          kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, startBuf));
          kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, endBuf));
          kjChildAdd(methodArray, tuple);
        }
      }
      else
      {
        // Multiple periods
        for (double pStart = periodStart; pStart < periodEnd; pStart += periodSeconds)
        {
          double pEnd = pStart + periodSeconds;
          if (pEnd > periodEnd)
            pEnd = periodEnd;

          Accumulator acc;
          accumulatorInit(&acc);

          for (KjNode* instanceP = attrP->value.firstChildP; instanceP != NULL; instanceP = instanceP->next)
          {
            if (instanceP->type != KjObject)
              continue;

            double obsAt = getObservedAt(instanceP);
            if (obsAt >= pStart && obsAt < pEnd)
            {
              double val;
              if (getNumericValue(instanceP, &val))
                accumulatorAdd(&acc, val);
            }
          }

          if (acc.count > 0)
          {
            char startBuf[64], endBuf[64];
            epochToIso8601(pStart, startBuf, sizeof(startBuf));
            epochToIso8601(pEnd, endBuf, sizeof(endBuf));

            KjNode* tuple = kjArray(orionldState.kjsonP, NULL);
            kjChildAdd(tuple, kjFloat(orionldState.kjsonP, NULL, accumulatorResult(&acc, methods[m])));
            kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, startBuf));
            kjChildAdd(tuple, kjString(orionldState.kjsonP, NULL, endBuf));
            kjChildAdd(methodArray, tuple);
          }
        }
      }

      kjChildAdd(aggrObj, methodArray);
    }

    // Replace in-place: change attrP to be the aggregation object
    attrP->type  = aggrObj->type;
    attrP->value = aggrObj->value;
  }
}
