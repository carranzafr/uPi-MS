#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import os
import re
import time

import wfd
import core_channel
import sink

from util import get_stdout

cmd_wlan0_up = 'ifup wlan0'
cmd_inc_rmem_default = 'sysctl -w net.core.rmem_default=1000000'
cmd_launch_core_app = 'nice -n -20 ./core'
cmd_kill_core_app = 'killall core'
cmd_dhcp_start = 'service isc-dhcp-server start'
cmd_dhcp_stop = 'service isc-dhcp-server stop'

lease_file = '/var/lib/dhcp/dhcpd.leases'

def lease_file_timestamp_get():
    return get_stdout('ls -l "%s"' % lease_file)

# get the leased IP address
def leased_ip_get():
    contents = open(lease_file).read()
    ip_list = re.findall(r'lease (\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})', contents)

    # return the most recently leased IP address
    return ip_list[-1]

print 'Bring up wlan0 just in case...'
get_stdout(cmd_wlan0_up)

print 'Increase rmem_default...'
get_stdout(cmd_inc_rmem_default)

while 1:
    # Start DHCP
    print get_stdout(cmd_dhcp_start)

    # Get previous timestamp
    prev_ts = lease_file_timestamp_get()

    # Wait for connection
    wfd.wfd_connection_wait()

    # Wait until lease file is updated
    while 1:

        curr_ts = lease_file_timestamp_get()

        if curr_ts != prev_ts:

            print 'Source has requested IP!'

            # wait for network to be properly configured
            time.sleep(2)

            break

        print 'lease table has not been updated, wait for a second...'

        time.sleep(1)

    # Get source IP
    ip = leased_ip_get()

    print 'leased IP: ', ip

    # Connect to source
    sink.source_connect(ip)

    # Stop DHCPd
    output = get_stdout(cmd_dhcp_stop)

    # Kill app
    output = get_stdout(cmd_kill_core_app)
    print output

