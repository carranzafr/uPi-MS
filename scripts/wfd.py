# -*- coding: UTF-8 -*-
import re
import time

from util import get_stdout

cmd_killall_wpa_spplicant   = 'killall wpa_supplicant'
cmd_killall_hostapd         = 'killall hostapd'
cmd_iwlist_wlan0_scan       = 'iwlist wlan0 scan'

def peer_mac_get():
    output = get_stdout('iwpriv wlan0 p2p_get peer_ifa')
    match = re.search(r'MAC (.*)$', output)
    return match.group(1)

def wpa_supplicant_start():
    print 'wpa_supplicant_start:'
    get_stdout(["./wpa_supplicant", "-i", "wlan0", "-c", "./wpa_0_8.conf", "-B"])
    time.sleep(1)

def wps_auth():
    print 'wps_auth:'
    output = get_stdout(["./hostapd_cli", "wps_pbc", "any"])
    print output
    time.sleep(1)

def wps_status_get():
    print 'wps_status_get:'
    output = get_stdout(["./wpa_client", "status"])
    print output

def p2p_wpsinfo():
    print 'p2p_wpsinfo:'
    get_stdout('iwpriv wlan0 p2p_set got_wpsinfo=3')

def p2p_status_get():
    output = get_stdout('iwpriv wlan0 p2p_get status')
    match = re.search(r'Status=(\d*)', output)
    return int(match.group(1))

def p2p_set_nego(mac):
    print 'p2p_set_nego:'
    print 'mac: %s' % mac
    get_stdout('iwpriv wlan0 p2p_set nego=%s' % mac)

    # Enter negotiation loop
    while 1:

        # Wait for result
        time.sleep(0.5)

        # Poll status
        peer_status = p2p_status_get()
        print 'peer_status: %d' % peer_status

        # For Windows 8.1 support, we consider 19 as negotiation completed
        if peer_status in [10, 19]:
            print 'Negotiation suceeded!'
            break

    # Get role
    role = p2p_role_get()
    print 'Role: %s' % role

    # Get peer interface address
    peer_mac_get()

    p2p_go_mode_set()

# p2p_enable Enable wifi direct
def p2p_enable():

    # Enable p2p
    get_stdout('iwpriv wlan0 p2p_set enable=1')

    # Set intent
    get_stdout('iwpriv wlan0 p2p_set intent=15')

    # Set operation channel
    get_stdout('iwpriv wlan0 p2p_set op_ch=%d' % 11)

    # Sleep for 50ms
    time.sleep(0.05)

    # Set ssid
    get_stdout('iwpriv wlan0 p2p_set ssid=DIRECT-RT')

    # Set DN
    get_stdout('iwpriv wlan0 p2p_set setDN=Piracast')

# p2p_peer_devaddr_get Gets peer device address
def p2p_peer_devaddr_get():
    print 'p2p_peer_devaddr_get:'
    output = get_stdout(["iwpriv", "wlan0", "p2p_get", "peer_deva"])
    match = re.search(r'\n(.*)$', output)
    mac = ':'.join(re.findall('..', match.group(1)))

    return mac

# p2p_req_cm_get Gets supported authentication type
def p2p_req_cm_get():
    print 'p2p_req_cm_get:'
    print get_stdout('iwpriv wlan0 p2p_get req_cm')

def p2p_role_get():
    print 'p2p_role_get:'
    output = get_stdout('iwpriv wlan0 p2p_get role')
    match = re.search(r'Role=(\d*)', output)
    role = int(match.group(1))
    return role

def p2p_opch_get():
    print 'p2p_opch_get:'
    print '---------------------------'
    output = get_stdout('iwpriv wlan0 p2p_get op_ch')
    print output
    print '---------------------------'

def p2p_go_mode_set():

    # Start hostAPd and wait for it to daemonize; ignore stdout
    get_stdout(["./hostapd", "-B", "p2p_hostapd.conf"])

    # Wait for initialization
    time.sleep(1)

    do_wps()

    # Wait for host apd interval
    time.sleep(1)

    while 1:
        status = read_all_sta()

        if status:
            print 'Wireless display negotiation completed!'
            break

        time.sleep(1)

def do_wps():

    while 1:
        print 'do_wps:'
        output = get_stdout(["./hostapd_cli", "wps_pbc", "any"])

        print output

        if 'OK' in output:
            print 'wps passed!'
            return

        time.sleep(1)

def read_all_sta():
    print 'read_all_sta:'
    output = get_stdout(["./hostapd_cli", "all_sta"])
    return ('dot11RSNAStatsSTAAddress' in output)

def p2p_disable():
    get_stdout('iwpriv wlan0 p2p_set enable=0')

def p2p_peer_scan():
    count = 0

    while 1:
        output = get_stdout(cmd_iwlist_wlan0_scan)

        print output

        if 'No scan results' not in output:
            return True

        if count > 3:
            return False

        count += 1

def wfd_connection_wait():
    get_stdout(cmd_killall_wpa_spplicant)
    get_stdout(cmd_killall_hostapd)

    # Disable p2p
    p2p_disable()

    time.sleep(0.5)

    # Enable p2p
    p2p_enable()

    #p2p_peer_scan()

    print 'Waiting for incoming connection...'

    while 1:

        peer_status = p2p_status_get()

        print 'peer_status: %d' % peer_status

        if peer_status == 0:
            print 'p2p disabled! Re-enable p2p...'
            p2p_enable()

        if peer_status in [8, 19, 22]:
            # Discovery request or gonego fail
            print 'Discovery request received!'
            peer_found = p2p_peer_scan()

            if peer_found:
                break

            p2p_disable()

        time.sleep(1)

    print 'Getting peer device address...'

    # Get peer device address
    mac = p2p_peer_devaddr_get()
    print 'peer_devaddr: %s' % mac

    # Notify received wps info
    p2p_wpsinfo()

    print 'Getting peer authentication type...'

    # Get request configuration
    p2p_req_cm_get()

    print 'Confirming peer authentication...'

    # Set negotiation
    p2p_set_nego(mac)
