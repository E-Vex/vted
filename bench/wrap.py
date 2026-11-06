#!/usr/bin/env python3
"""bench/wrap.py — time the editor laying very long single lines out.

The editor is spawned on a pseudo terminal the way packaging/
smoke-test.py does it, 24 rows by 80 columns, with a file of its own
the walk generates once and keeps (sixty-four rows, sixteen kilobytes
of ordinary words each, no line feed inside a row) — a row wraps into
a couple of hundred screen lines, the way a line of minified text or
a CSV export does. Four clocks are taken per run:

  first frame   the process starting to the first painted frame —
                told from the setup escapes by the bar of the
                opening view, the load of the whole file inside it
  wrap on       one Ctrl-L, timed to the frame that carries the
                "Wrap on" in the bar — the long row laid out wrapped
                behind it
  page down     thirty Page Downs through the wrapped layout from
                there, each page one key, timed from its write to the
                last byte of the frame it drew — the walk crosses
                three row boundaries inside it and spends most of its
                pages deep in a tall row, where every screen line it
                paints is laid out from the row's own start
  page up       thirty Page Ups back up the same way

The first frame and the wrap are medians over the runs; the page
clocks pool every step of every run and take the median of those.

  python3 bench/wrap.py ./vted [file] [runs] [pages]
"""
import os
import pty
import select
import statistics
import struct
import sys
import termios
import fcntl
import time

CTRL_L = b'\x0c'
CTRL_Q = b'\x11'
PAGE_DOWN = b'\x1b[6~'
PAGE_UP = b'\x1b[5~'

BENCH_FILE = '/tmp/vted_bench_longlines.txt'
ROWS = 64          # the rows of the file the walk keeps
ROW_BYTES = 16384  # the bytes of every one of them, no line feed inside
SENTENCE = 'The quick brown fox jumps over the lazy dog'


def generate_file(path):
    """the file the walk keeps: very long single lines of ordinary
    words, each row ending in its own number — the wrap layout breaks
    at the word boundaries the way it does for ordinary text"""
    fill = (SENTENCE + ' ') * (ROW_BYTES // len(SENTENCE) + 2)
    with open(path, 'w') as f:
        for row in range(ROWS):
            cut = fill.rfind(' ', 0, ROW_BYTES - 8)
            f.write(fill[:cut] + ' %06d\n' % (row + 1))


def spawn(binary, args):
    """the editor on a pseudo terminal of 24 rows and 80 columns"""
    pid, fd = pty.fork()
    if pid == 0:  # child: run the editor, far from anything it might touch
        os.chdir('/')
        os.execv(binary, ['vted'] + args)
        os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    return pid, fd


def wait_exit(pid, fd, timeout=10):
    """the editor's exit status once it left on its own, or None when
    it had to be killed after timeout seconds"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            os.close(fd)
            return status
        time.sleep(0.001)  # 1 ms: the exit clock keeps its resolution

    os.kill(pid, 9)
    os.waitpid(pid, 0)
    os.close(fd)
    return None


def step(fd, keys, telltale=None, quiet=0.05, timeout=5.0):
    """(seconds from the write to the telltale — or to the last byte
    of the frame, when no telltale is given — and the bytes it drew),
    or (None, bytes) when nothing came back in time"""
    start = time.perf_counter()
    os.write(fd, keys)
    out = b''
    last = None

    while time.perf_counter() - start < timeout:
        ready, _, _ = select.select([fd], [], [], 0.01)
        if ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break  # the editor left; its exit status says
            if not chunk:
                break
            out += chunk
            last = time.perf_counter()
            if telltale is not None and telltale in out:
                break
        elif (telltale is None and last is not None
              and time.perf_counter() - last >= quiet):
            break

    if last is None:
        return None, out
    return last - start, out


def one_run(binary, file_arg, pages):
    """(first frame, wrap on, page downs, page ups) in seconds, or
    None when a run went wrong"""
    pid, fd = spawn(binary, [file_arg])

    # the first painted frame with the layout the editor opens in —
    # wrap off, the whole file one endless line a row: the load of
    # the file is inside the number
    first, _ = step(fd, b'', telltale=b'Ln 1/', quiet=0.3, timeout=15.0)

    if first is None:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return None

    # Ctrl-L turns the wrap on: the frame that carries the "Wrap on"
    # in the bar is drawn over the long row laid out wrapped
    wrap, _ = step(fd, CTRL_L, telltale=b'Wrap on', timeout=5.0)

    downs = []
    ups = []

    if wrap is not None:
        for _ in range(pages):
            got, _ = step(fd, PAGE_DOWN, quiet=0.05, timeout=2.0)
            if got is None:
                break
            downs.append(got)

        for _ in range(pages):
            got, _ = step(fd, PAGE_UP, quiet=0.05, timeout=2.0)
            if got is None:
                break
            ups.append(got)

    try:
        os.write(fd, CTRL_Q)
    except OSError:
        pass  # the editor may have left already; its exit status says

    status = wait_exit(pid, fd)
    if status is None or not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return None
    if wrap is None or len(downs) != pages or len(ups) != pages:
        return None

    return first, wrap, downs, ups


def main() -> int:
    if len(sys.argv) < 2:
        print(f'usage: {sys.argv[0]} <path-to-vted> [file] [runs] [pages]',
              file=sys.stderr)
        return 2

    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'bench: {binary} is not executable', file=sys.stderr)
        return 2

    file_arg = sys.argv[2] if len(sys.argv) > 2 else BENCH_FILE
    if not os.path.exists(file_arg):
        if os.path.abspath(file_arg) != BENCH_FILE:
            print(f'bench: {file_arg} is missing', file=sys.stderr)
            return 2
        generate_file(BENCH_FILE)  # made once and kept, the way make bench keeps its own

    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    pages = int(sys.argv[4]) if len(sys.argv) > 4 else 30

    file_arg = os.path.abspath(file_arg)  # the child chdirs to /
    firsts = []
    wraps = []
    downs = []
    ups = []

    for _ in range(runs):
        result = one_run(binary, file_arg, pages)
        if result is None:
            print('bench: a run went wrong', file=sys.stderr)
            return 1
        firsts.append(result[0])
        wraps.append(result[1])
        downs += result[2]
        ups += result[3]

    print(f'runs: {runs}, pages each: {pages}')
    print(f'first frame: median {statistics.median(firsts) * 1e3:.1f} ms '
          f'(min {min(firsts) * 1e3:.1f}, max {max(firsts) * 1e3:.1f})')
    print(f'wrap on:     median {statistics.median(wraps) * 1e3:.1f} ms '
          f'(min {min(wraps) * 1e3:.1f}, max {max(wraps) * 1e3:.1f})')
    print(f'page down:   median {statistics.median(downs) * 1e3:.1f} ms '
          f'(min {min(downs) * 1e3:.1f}, max {max(downs) * 1e3:.1f}) '
          f'over {len(downs)} pages')
    print(f'page up:     median {statistics.median(ups) * 1e3:.1f} ms '
          f'(min {min(ups) * 1e3:.1f}, max {max(ups) * 1e3:.1f}) '
          f'over {len(ups)} pages')
    return 0


if __name__ == '__main__':
    sys.exit(main())
