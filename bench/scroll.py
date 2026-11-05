#!/usr/bin/env python3
"""bench/scroll.py — time the editor paging a large file.

The editor is spawned on a pseudo terminal the way packaging/
smoke-test.py does it, 24 rows by 80 columns, with the file `make
bench` keeps (about a hundred megabytes of text, two million rows)
opened by its absolute path. Four clocks are taken per run:

  first frame   the process starting to the first painted frame —
                the load of the whole file is inside the number
  go to line    one Ctrl-G asking for a line a screenful short of
                the million — six-digit gutter country — timed from
                the write to the frame that carries the bar of the
                deep line ("Ln 999976/...")
  page down     thirty Page Downs from there — the walk crosses the
                million-line boundary, so the gutter grows a digit
                inside it — each page one key, timed from its write
                to the last byte of the frame it drew
  page up       thirty Page Ups back up the same way

The first frame and the go-to are medians over the runs; the page
clocks pool every step of every run and take the median of those.

  python3 bench/scroll.py ./vted [file] [runs] [pages]
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

CTRL_G = b'\x07'
CTRL_Q = b'\x11'
ENTER = b'\r'
PAGE_DOWN = b'\x1b[6~'
PAGE_UP = b'\x1b[5~'

GO_TO = 999976  # a screenful short of the million: the gutter grows inside the walk


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
    """(first frame, go to, page downs, page ups) in seconds, or None
    when a run went wrong"""
    pid, fd = spawn(binary, [file_arg])

    # the first painted frame, not the setup escapes before it: the
    # bar of the opening view ("Ln 1/...") only arrives once the
    # whole file is loaded and laid out behind it
    first, _ = step(fd, b'', telltale=b'Ln 1/', quiet=0.3, timeout=15.0)

    if first is None:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return None

    # Ctrl-G asks for the line, Enter takes it; the next frame
    # carries the bar of the deep line
    digits = str(GO_TO).encode()
    goto, _ = step(fd, CTRL_G + digits + ENTER,
                   telltale=b'Ln ' + digits + b'/', timeout=5.0)

    downs = []
    ups = []

    if goto is not None:
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
    if goto is None or len(downs) != pages or len(ups) != pages:
        return None

    return first, goto, downs, ups


def main() -> int:
    if len(sys.argv) < 2:
        print(f'usage: {sys.argv[0]} <path-to-vted> [file] [runs] [pages]',
              file=sys.stderr)
        return 2

    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'bench: {binary} is not executable', file=sys.stderr)
        return 2

    file_arg = sys.argv[2] if len(sys.argv) > 2 else '/tmp/vted_bench_100mb.txt'
    if not os.path.exists(file_arg):
        print(f'bench: {file_arg} is missing — run make bench first',
              file=sys.stderr)
        return 2

    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    pages = int(sys.argv[4]) if len(sys.argv) > 4 else 30

    file_arg = os.path.abspath(file_arg)  # the child chdirs to /
    firsts = []
    gotos = []
    downs = []
    ups = []

    for _ in range(runs):
        result = one_run(binary, file_arg, pages)
        if result is None:
            print('bench: a run went wrong', file=sys.stderr)
            return 1
        firsts.append(result[0])
        gotos.append(result[1])
        downs += result[2]
        ups += result[3]

    print(f'runs: {runs}, pages each: {pages}')
    print(f'first frame: median {statistics.median(firsts) * 1e3:.1f} ms '
          f'(min {min(firsts) * 1e3:.1f}, max {max(firsts) * 1e3:.1f})')
    print(f'go to line {GO_TO}: median {statistics.median(gotos) * 1e3:.1f} ms '
          f'(min {min(gotos) * 1e3:.1f}, max {max(gotos) * 1e3:.1f})')
    print(f'page down: median {statistics.median(downs) * 1e3:.1f} ms '
          f'(min {min(downs) * 1e3:.1f}, max {max(downs) * 1e3:.1f}) '
          f'over {len(downs)} pages')
    print(f'page up:   median {statistics.median(ups) * 1e3:.1f} ms '
          f'(min {min(ups) * 1e3:.1f}, max {max(ups) * 1e3:.1f}) '
          f'over {len(ups)} pages')
    return 0


if __name__ == '__main__':
    sys.exit(main())
