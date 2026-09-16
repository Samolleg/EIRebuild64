#!/usr/bin/env python3
import os
import subprocess

CLANG_FORMAT_PATH = 'clang-format'
CLANG_FORMAT_ARGS = [CLANG_FORMAT_PATH, '-i', '-style=file']

SKIP_FOLDERS = ["SDKs", "packages", ".git", "build"]

for root, directories, filenames in os.walk('.'):    
    directories[:] = [d for d in directories if d not in SKIP_FOLDERS]
    for filename in filenames:        
        path = os.path.join(root, filename)        
        print(path)
        _, ext = os.path.splitext(path)
        if ext.lower() in ['.c', '.cpp', '.h', '.inl']:
            subprocess.check_call(CLANG_FORMAT_ARGS + [path])
