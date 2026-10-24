#!/usr/bin/env python3
import base64
import gzip
import pathlib
import sys

src = pathlib.Path(__file__).with_name("main_r7.cpp.gz.b64")
out = pathlib.Path(sys.argv[1])
out.write_bytes(gzip.decompress(base64.b64decode(src.read_text().strip())))
