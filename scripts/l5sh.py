#!/usr/bin/env python3
# Run one shell command on the test initramfs via busybox telnetd (10.66.0.1:23).
import socket, sys, time, re
cmd, wait = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 3
s = socket.create_connection(("10.66.0.1", 23), timeout=5)
def strip_iac(b):
    out, i = bytearray(), 0
    while i < len(b):
        if b[i] == 255 and i + 1 < len(b):
            if b[i+1] in (251, 252, 253, 254): 
                # refuse every option
                s.send(bytes([255, 252 if b[i+1] in (253, 254) else 254, b[i+2]])) if i + 2 < len(b) else None
                i += 3; continue
            i += 2; continue
        out.append(b[i]); i += 1
    return bytes(out)
def drain(t):
    s.settimeout(0.3); buf = b""; end = time.time() + t
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d: break
            buf += strip_iac(d)
        except socket.timeout: pass
    return buf
drain(1)
s.send(b"stty -echo 2>/dev/null; echo __BEGIN__; " + cmd.encode() + b"; echo __END__\r\n")
out = drain(wait).decode(errors="replace").replace("\r", "")
m = re.search(r"__BEGIN__\n(.*?)__END__", out, re.S)
print(m.group(1) if m else out)
