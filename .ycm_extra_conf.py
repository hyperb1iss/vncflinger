# Vim YouCompleteMe completion configuration.
#
# See doc/topics/ycm.md for details.

import os

repo_root = os.path.dirname(os.path.abspath(__file__))

# Paths in the compilation flags must be absolute to allow ycm to find them from
# any working directory.
def AbsolutePath(path):
  return os.path.join(repo_root, path)

flags = [
    '-I', AbsolutePath('src'),
    '-I', AbsolutePath('../tigervnc/common'),
    '-I', AbsolutePath('../libvncserver'),
    '-I', AbsolutePath('../../system/core/include'),
    '-I', AbsolutePath('../../frameworks/native/include'),
    '-Wall',
    '-Werror',
    '-Wextra',
    '-pedantic',
    '-Wno-newline-eof',
    '-Wwrite-strings',
    '-std=c++11',
    '-x', 'c++'
]

def FlagsForFile(filename, **kwargs):
    return {
        'flags': flags,
        'do_cache': True
    }

