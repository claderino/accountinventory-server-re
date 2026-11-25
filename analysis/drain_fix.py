import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\AccountInventoryDB.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''    SQLRETURN rc = s.Execute();
    return (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) ? s.ReturnValue() : -1;'''
new = '''    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();'''
n = s.count(old)
s = s.replace(old, new)
io.open(p, 'w', encoding='utf-8').write(s)
print('patched', n, 'methods')
