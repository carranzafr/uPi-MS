import os
import shutil
import subprocess

# Change to target directory
os.chdir("../target") 
 
# Compile application
cmd_make_app = 'make clobber app'
os.system(cmd_make_app)
 
# Change to application directory
os.chdir("../application") 
 
# Freeze main
print 'Freezing python application...'
cmd_freeze_main = 'cxfreeze main.py'
os.system(cmd_freeze_main)
 
# Go back to release directory
os.chdir("../release") 

print 'Creating directories...'
cmd_mkdir_upi = 'mkdir upi'
os.system(cmd_mkdir_upi)
 
os.chdir("./upi")
cmd_mkdir_application = 'mkdir application'
os.system(cmd_mkdir_application)
 
cmd_mkdir_installer = 'mkdir installer'
os.system(cmd_mkdir_installer)
 
# Go back to release directory
os.chdir("../") 
 
# Copy application files
paths = ['../application/hostapd', 
         '../application/hostapd_client',
         '../application/p2p_hostapd.conf', 
         '../application/wpa_0_8.conf', 
         '../application/hostapd_client',
         '../application/wpa_supplicant', 
         '../application/app', 
         '../application/dist/main']
 
for path in paths : 
   shutil.copy(path, 'upi/application')

# Copy installer files
installs = ['../environment/8188eu.ko', 
            '../environment/isc-dhcp-server', 
            '../environment/dhcpd.conf', 
            '../environment/interfaces', 
            '../environment/setup.py']
 
for install in installs : 
   shutil.copy(install, 'upi/installer')
   
# Copy instruction files
instructions = [
            '../environment/README'
               ]
 
for instruction in instructions : 
   shutil.copy(instruction, 'upi')
    
print 'Create tar package...'
cmd_tar_upi = 'tar -zcvf upi.tar.gz upi'
os.system(cmd_tar_upi)
 
print 'Release package ready!' 
