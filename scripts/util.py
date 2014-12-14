# -*- coding: UTF-8 -*-
import subprocess

def get_stdout(args):
    cmd = subprocess.Popen(args, shell=isinstance(args, basestring), stdout=subprocess.PIPE)
    (stdoutdata, stderrdata) = cmd.communicate()
    return stdoutdata
