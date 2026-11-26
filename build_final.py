# -*- coding: utf-8 -*-
"""Сборка пакета БЕЗ OPDS из уже установленного KoboRoot.tgz.

Берёт текущий KoboRoot.tgz, удаляет всё из .adds/opds и заменяет
.adds/nm/kofm на вариант из res/nm_kofm (два пункта). Остальное сохраняет
побайтно, включая libkofm.so, Kobalt, noreg и русскую локализацию.

Без FORCE=1 существующий KoboRoot.tgz не перезаписывается.
Для проверки: FINAL_OUT=путь\test.tgz python build_final.py
"""
import copy
import hashlib
import io
import os
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.environ.get('FINAL_BASE') or os.path.join(HERE, 'KoboRoot.tgz')
NM_FILE = os.path.join(HERE, 'res', 'nm_kofm')
OUT = os.environ.get('FINAL_OUT') or os.path.join(HERE, 'KoboRoot.tgz')
NM_NAME = './mnt/onboard/.adds/nm/kofm'
OPDS_PREFIX = './mnt/onboard/.adds/opds/'


def sha(data):
    return hashlib.sha256(data).hexdigest().upper()


def main():
    for path in (BASE, NM_FILE):
        if not os.path.isfile(path):
            raise SystemExit('Нет файла: ' + path)
    if os.path.exists(OUT) and os.environ.get('FORCE') != '1' and not os.environ.get('FINAL_OUT'):
        raise SystemExit('KoboRoot.tgz уже есть; для перезаписи задайте FORCE=1')
    nm = open(NM_FILE, 'rb').read()

    tmp = OUT + '.tmp'
    with tarfile.open(BASE, 'r:gz') as tb, tarfile.open(tmp, 'w:gz', compresslevel=9) as to:
        for m in tb.getmembers():
            if m.name.startswith(OPDS_PREFIX):
                continue
            if m.isdir():
                to.addfile(copy.copy(m))
                continue
            if not m.isfile():
                continue
            if m.name == NM_NAME:
                ti = copy.copy(m)
                ti.size = len(nm)
                to.addfile(ti, io.BytesIO(nm))
            else:
                to.addfile(copy.copy(m), tb.extractfile(m))
    os.replace(tmp, OUT)

    print('OUT:', OUT)
    print('SIZE:', os.path.getsize(OUT))
    with tarfile.open(OUT, 'r:gz') as t:
        names = [m.name for m in t.getmembers() if m.isfile()]
        print('ENTRIES:', len(t.getmembers()))
        print('OPDS записей:', len([n for n in names if n.startswith(OPDS_PREFIX)]))
        data = t.extractfile(NM_NAME).read()
        print('OK  nm/kofm', len(data), sha(data))
        print(data.decode('utf-8'), end='')
        for name, label in [
            ('./usr/local/Kobo/imageformats/libkofm.so', 'libkofm.so'),
            ('./usr/local/Kobo/imageformats/libkobalt.so', 'libkobalt.so'),
            ('./usr/local/Kobo/imageformats/libnoreg.so', 'libnoreg.so v4'),
            ('./usr/local/Kobo/translations/trans_ru.qm', 'trans_ru.qm'),
        ]:
            d = t.extractfile(name).read()
            print('OK  ', label, len(d), sha(d))


if __name__ == '__main__':
    main()
