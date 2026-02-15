/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
* Author: Ken Zangelin
*/
#include <stddef.h>                                              // NULL

extern "C"
{
#include "kprom/kprom.h"                                         // kprom API
}

#include "orionld/prometheus/promServer.h"                       // promServerStart
#include "orionld/prometheus/promInit.h"                         // Own interface



// -----------------------------------------------------------------------------
//
// Prometheus counters
//
KpromMetric* promNgsildRequests       = NULL;
KpromMetric* promNgsildRequestsFailed = NULL;
KpromMetric* promNotifications        = NULL;
KpromMetric* promNotificationsFailed  = NULL;
KpromMetric* promDistOps              = NULL;
KpromMetric* promDistOpsFailed        = NULL;

// Gauges
KpromMetric* promConnectionsActive    = NULL;
KpromMetric* promSubscriptionsCached  = NULL;

// Histograms
KpromMetric* promRequestDuration      = NULL;



// -----------------------------------------------------------------------------
//
// promInit - initialize the Prometheus metrics and start the metrics server
//
int promInit(unsigned short promPort)
{
  // Counters
  promNgsildRequests       = kpromCounterCreate("ngsildRequests",       "NGSILD Requests");
  promNgsildRequestsFailed = kpromCounterCreate("ngsildRequestsFailed", "Failed NGSILD Requests");
  promNotifications        = kpromCounterCreate("notifications",        "Notifications");
  promNotificationsFailed  = kpromCounterCreate("notificationsFailed",  "Failed Notifications");
  promDistOps              = kpromCounterCreate("distOps",              "Forwarded Distributed Operations");
  promDistOpsFailed        = kpromCounterCreate("distOpsFailed",        "Failed Distributed Operations");

  // Gauges
  promConnectionsActive    = kpromGaugeCreate("connectionsActive",      "Active HTTP connections");
  promSubscriptionsCached  = kpromGaugeCreate("subscriptionsCached",    "Subscriptions in cache");

  // Histograms - request duration in seconds
  // Buckets: 1ms, 2ms, 3ms, 4ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s, 10s
  double durationBuckets[] = { 0.001, 0.002, 0.003, 0.004, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 5.0, 10.0 };
  promRequestDuration      = kpromHistogramCreate("requestDurationSeconds", "Request duration in seconds", durationBuckets, 14);

  // Start the metrics server on the specified port
  if (promPort != 0)
    return promServerStart(promPort);

  return 0;
}
