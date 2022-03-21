#!/usr/bin/env .venv/bin/python
# -*- coding: latin-1 -*-
# Copyright 2013 Telefonica Investigacion y Desarrollo, S.A.U
#
# This file is part of Orion Context Broker.
#
# Orion Context Broker is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# Orion Context Broker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
# General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
#
# For those usages not covered by this license please contact with
# iot_support at tid dot es

from __future__ import division   # need for seconds calculation (could be removed with Python 2.7)

__author__ = 'fermin'

# This program stores everything it receives by HTTP in a given URL (pased as argument),
# Then return the accumulated data upon receiving 'GET <host>:<port>/dump'. It is aimet
# at harness test for subscription scenarios (so accumulator-server.py plays the role
# of a subscribed application)
#
# Known issues:
#
# * Curl users: use -H "Expect:" (default "Expect: 100-continue" used by curl has been problematic
#   in the past)
# * Curl users: use -H "Content-Type: application/xml"  for XML payload (the default:
#   "Content-Type: application/x-www-form-urlencoded" has been problematic in the pass)

from OpenSSL import SSL
from flask import Flask, request, Response
from getopt import getopt, GetoptError
from datetime import datetime
from math import trunc
from time import sleep
from sys import exit, argv, stderr
from os.path import basename, isfile
from os import unlink, getpid, kill
from atexit import register
from signal import SIGTERM, SIGINT, SIGKILL
from json import loads, dumps


def usage_and_exit(msg):
    """
    Print usage message and exit"

    :param msg: optional error message to print
    """

    if msg != '':
        print("{}\n".format(msg))

    usage()
    exit(1)


def usage():
    """
    Print usage message
    """

    print('Usage: %s --host <host> --port <port> --url <server url> --pretty-print -v -u\n'
          % basename(__file__))

    print('Parameters:')
    print("  --host <host>: host to use database to use (default is '0.0.0.0')")
    print("  --port <port>: port to use (default is 1028)")
    print("  --url <server url>: server URL to use (default is /accumulate)")
    print("  --pretty-print: pretty print mode")
    print("  --https: start in https")
    print("  --key: key file (only used if https is enabled)")
    print("  --cert: cert file (only used if https is enabled)")
    print("  -v: verbose mode")
    print("  -u: print this usage message")


# This function is registered to be called upon termination
def all_done():
    unlink(pidFile)


# Default arguments
port = 1028
host = '0.0.0.0'
server_url = '/accumulate'
verbose = 0
pretty = False
https = False
key_file = None
cert_file = None

opts = []
try:
    opts, args = getopt(argv[1:], 'vu', ['host=', 'port=', 'url=', 'pretty-print', 'https', 'key=', 'cert='])
except GetoptError:
    usage_and_exit('wrong parameter')

for opt, arg in opts:
    if opt == '-u':
        usage()
        exit(0)
    elif opt == '--host':
        host = arg
    elif opt == '--url':
        server_url = arg
    elif opt == '--port':
        try:
            port = int(arg)
        except ValueError:
            usage_and_exit('port parameter must be an integer')
    elif opt == '-v':
        verbose = 1
    elif opt == '--pretty-print':
        pretty = True
    elif opt == '--https':
        https = True
    elif opt == '--key':
        key_file = arg
    elif opt == '--cert':
        cert_file = arg
    else:
        usage_and_exit(msg='')

if https:
    if key_file is None or cert_file is None:
        print("if --https is used then you have to provide --key and --cert")
        exit(1)

if verbose:
    print("verbose mode is on")
    print("port: " + str(port))
    print("host: " + str(host))
    print("server_url: " + str(server_url))
    print("pretty: " + str(pretty))
    print("https: " + str(https))
    if https:
        print("key file: " + key_file)
        print("cert file: " + cert_file)

pid = str(getpid())
pidFile = "/tmp/accumulator." + str(port) + ".pid"

#
# If an accumulator process is already running, it is killed.
# First using SIGTERM, then SIGINT and finally SIGKILL
# The exception handling is needed as this process dies in case
# a kill is issued on a non-running process ...
#
if isfile(pidFile):
    oldPid = open(pidFile, 'r').read()
    oPid = int(oldPid)
    print("PID file %s already exists, killing the process %s" % (pidFile, oldPid))

    try: 
        oldStderr = stderr
        stderr = open("/dev/null", "w")
        kill(oPid, SIGTERM)
        sleep(0.1)
        kill(oPid, SIGINT)
        sleep(0.1)
        kill(oPid, SIGKILL)
        stderr = oldStderr  # !!!
    except OSError:
        print("Process %d killed" % oPid)


#
# Creating the pidFile of the currently running process
#
open(pidFile, 'w').write(pid)

#
# Making the function all_done being executed on exit of this process.
# all_done removes the pidFile
#
register(all_done)


app = Flask(__name__)


@app.route("/noresponse", methods=['POST'])
def no_response():
    sleep(10)
    return Response(status=200)


@app.route("/noresponse/updateContext", methods=['POST'])
def uno_response():
    sleep(10)
    return Response(status=200)


@app.route("/noresponse/queryContext", methods=['POST'])
def qno_response():
    sleep(10)
    return Response(status=200)


# This response has been designed to test the #2360 case, but is general enough to be
# used in other future cases
@app.route("/badresponse/queryContext", methods=['POST'])
def bad_response():
    r = Response(status=404)
    r.data = '{"name":"ENTITY_NOT_FOUND","message":"The entity with the requested id [qa_name_01] was not found."}'
    return r


def record_request(req):
    """
    Common function used by several route methods to save request content

    :param req: the request to save
    """

    global ac, t0, times
    s = ''

    # First request? Then, set reference datetime. Otherwise, add the
    # timedelta to the list
    if t0 == '':
        t0 = datetime.now()
        times.append(0)
    else:
        delta = datetime.now() - t0
        # Python 2.7 could use delta.total_seconds(), but we use this formula
        # for backward compatibility with Python 2.6
        t = (delta.microseconds + (delta.seconds + delta.days * 24 * 3600) * 10 ** 6) / 10 ** 6
        times.append(trunc(round(t)))
        # times.append(t)

    # Store verb and URL
    #
    # We have found that request.url can be problematic in some distributions (e.g. Debian 8.2)
    # when used with IPv6, so we use request.scheme, request.host and request.path to compose
    # the URL "manually"
    #
    #  request.url = request.scheme + '://' + request.host + request.path
    #
    s += req.method + ' ' + req.scheme + '://' + req.host + req.path

    # Check for query params
    params = ''
    for k in req.args:
        if params == '':
            params = k + '=' + req.args[k]
        else:
            params += '&' + k + '=' + req.args[k]

    if params == '':
        s += '\n'
    else:
        s += '?' + params + '\n'

    # Store headers
    for h in req.headers.keys():
        s += h + ': ' + req.headers[h] + '\n'

    # Store payload
    if (req.data is not None) and (len(req.data) != 0):
        s += '\n'
        if pretty:
            raw = loads(req.data)
            s += dumps(raw, indent=4, sort_keys=True)
            s += '\n'
        else:
            s += req.data

    # Separator
    s += '=======================================\n'

    # Accumulate
    ac += s

    if verbose:
        print(s)


def send_continue(req):
    """
    Inspect request header in order to look if we have to continue or not

    :param req: the request to look
    :return: true if we  have to continue, false otherwise
    """

    for h in req.headers.keys():
        if (h == 'Expect') and (req.headers[h] == '100-continue'):
            return True  # send_continue = True

    return False


@app.route("/v1/updateContext", methods=['POST'])
@app.route("/v1/queryContext", methods=['POST'])
@app.route(server_url, methods=['GET', 'POST', 'PUT', 'DELETE', 'PATCH'])
def record():

    # Store request
    record_request(request)

    if send_continue(request):
        return Response(status=100)
    else:
        return Response(status=200)


@app.route("/bug2871/updateContext", methods=['POST'])
def record_2871():

    # Store request
    record_request(request)

    if send_continue(request):
        return Response(status=100)
    else:
        # Ad hoc response related with issue #2871, see https://github.com/telefonicaid/fiware-orion/issues/2871
        r = Response(status=200)

        # r.data = '{"contextResponses":[{"contextElement":{"attributes":[{"name":"turn","type":"string","value":""}],
        #            "id":"entity1","isPattern":false,"type":"device"},"statusCode":{"code":200,"reasonPhrase":"OK"}}]}'

        r.data = {
                    "contextResponses": [
                        {
                            "contextElement": {
                                "attributes": [
                                    {
                                        "name": "turn",
                                        "type": "string",
                                        "value": ""
                                    }
                                ],
                                "id": "entity1",
                                "isPattern": False,
                                "type": "device"
                            },
                            "statusCode": {
                                "code": 200,
                                "reasonPhrase": "OK"
                            }
                        }
                    ]
        }

        return r


@app.route('/dump', methods=['GET'])
def dump():
    return ac


@app.route('/times', methods=['GET'])
def get_times():
    return ', '.join(map(str, times)) + '\n'


@app.route('/number', methods=['GET'])
def number():
    return str(len(times)) + '\n'


@app.route('/reset', methods=['POST'])
def reset():
    global ac, t0, times
    ac = ''
    t0 = ''
    times = []
    return Response(status=200)


@app.route('/pid', methods=['GET'])
def get_pid():
    return str(getpid())


# This is the accumulation string
ac = ''
t0 = ''
times = []


if __name__ == '__main__':
    # Note that using debug=True breaks the procedure to write the PID into a file. In particular
    # makes the call to os.path.isfile(pidFile) return True, even if the file doesn't exist. Thus,
    # use debug=True below with care :)
    if https:
        # According to http://stackoverflow.com
        # /questions/28579142/attributeerror-context-object-has-no-attribute-wrap-socket/28590266, the
        # original way of using context is deprecated. New way is simpler. However, we are still testing this...
        # some environments fail in some configurations (the current one is an attempt to make this to work at jenkins)
        context = SSL.Context(SSL.SSLv23_METHOD)
        context.use_privatekey_file(key_file)
        context.use_certificate_file(cert_file)
        # context = (cert_file, key_file)
        app.run(host=host, port=port, debug=False, ssl_context=context)
    else:
        app.run(host=host, port=port)
