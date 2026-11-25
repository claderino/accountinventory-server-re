#!/usr/bin/env python3
import socket
REQ = ("POST /Warehouse.bnsgrnTH/GetGoodsCount STS/1.0\r\n"
       "l:189\r\n"
       "s:1\r\n"
       "\r\n"
       "<Request>\n<UserId>7144F687-BD7A-4CA7-B94A-C3F19644CDDF</UserId>\n"
       "<IncludeEmptyGameServerId>true</IncludeEmptyGameServerId>\n"
       "<IncludeEmptyCharacterId>true</IncludeEmptyCharacterId>\n"
       "</Request>\n")
s = socket.create_connection(('127.0.0.1', 6601), timeout=5)
s.sendall(REQ.encode())
s.settimeout(3)
data = b''
try:
    while True:
        d = s.recv(65536)
        if not d: break
        data += d
        if b'</Reply>' in data: break
except socket.timeout:
    pass
s.close()
print('bytes', len(data))
print(data.decode('utf-8', 'replace')[:600])
