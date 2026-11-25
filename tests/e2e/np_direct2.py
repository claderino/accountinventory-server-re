#!/usr/bin/env python3
import socket

def request(path, body, seq):
    return ("POST %s STS/1.0\r\nl:%d\r\ns:%d\r\n\r\n%s" % (path, len(body.encode()), seq, body)).encode()

s = socket.create_connection(('127.0.0.1', 6601), timeout=5)
s.settimeout(2)

def send(path, body, seq):
    s.sendall(request(path, body, seq))
    data = b''
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            data += d
            if b'</Reply>' in data or b'</Error' in data: break
    except socket.timeout:
        pass
    return data

connect = ("<Connect>\n<ConnType>401</ConnType>\n<Address>127.0.0.1</Address>\n"
           "<ProductType>0</ProductType>\n<AppIndex>1</AppIndex>\n<Epoch>817332865</Epoch>\n"
           "<Program>1820</Program>\n<Build>4000</Build>\n<Process>32212</Process>\n</Connect>\n")
print('connect:', send('/Sts/Connect', connect, 0)[:200])
reg = "<Message>\n<Protocol>Srv2GameWarehouse</Protocol>\n</Message>\n"
print('register:', send('/Gate/RegisterService', reg, 1)[:200])
body = ("<Request>\n<UserId>7144F687-BD7A-4CA7-B94A-C3F19644CDDF</UserId>\n"
        "<IncludeEmptyGameServerId>true</IncludeEmptyGameServerId>\n"
        "<IncludeEmptyCharacterId>true</IncludeEmptyCharacterId>\n</Request>\n")
r = send('/Warehouse.bnsgrnTH/GetGoodsCount', body, 2)
print('getcount:', r.decode('utf-8','replace')[:400])
s.close()
