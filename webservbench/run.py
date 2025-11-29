#!/usr/bin/env python3

import socket
import subprocess
import sys
import argparse
import os
import signal

parser = argparse.ArgumentParser()
parser.add_argument("--nginx", required=True, help="Command to run nginx")
parser.add_argument("--siege", required=True, help="Command to run siege")
args = parser.parse_args()

# Starting running nginx.
nginx = subprocess.Popen(args.nginx.split())

# Wait until we can connect.
port = 8443
while True:
    if x := nginx.poll() is not None:
        print("ERROR: nginx died before binding to port", file=sys.stderr)
        print(x, file=sys.stderr)
        exit(1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        conn = sock.connect(("127.0.0.1", port))
        sock.close()
        break
    except OSError:
        sock.close()
        pass

# Spawn the siege process.
siege = subprocess.Popen(args.siege.split())

# Wait for siege to finish.
siege.wait()
    
# Kill nginx.
nginx.send_signal(signal.SIGINT)
nginx.wait()
