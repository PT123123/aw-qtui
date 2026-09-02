path = 'src/todostore.cpp'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = '#include "apiclient.h"\n#include "theme.h"'
new = '#include "apiclient.h"\n#include "mockdata.h"\n#include "theme.h"'
if old in content:
    content = content.replace(old, new)
    print('mockdata.h include added')
else:
    print('include pattern not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)

# 修头文件里的 static
hpath = 'src/todostore.h'
with open(hpath, 'r', encoding='utf-8') as f:
    hcontent = f.read()
hcontent = hcontent.replace('static QString listIdToTag(qint64 listId);', 'QString listIdToTag(qint64 listId);')
with open(hpath, 'w', encoding='utf-8') as f:
    f.write(hcontent)
print('static removed from header')
print('Done')
