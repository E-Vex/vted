#!/usr/bin/env python3
"""bench/search.py — time the query landing its match.

The editor is spawned on a pseudo terminal the way packaging/
smoke-test.py does it, with the file `make bench` keeps (about a
hundred megabytes of text, two million rows) opened by its absolute
path. After the first painted frame the walk opens the search
(Ctrl-F) and types a query in one burst; the clock runs from the
write of the query to the frame that carries the count in the bar,
so the incremental searches of its prefixes are inside the number.
Two queries:

  a hit   the seven-digit suffix of the last line — one match, the
          deepest row of the file, the search wrapping the whole
          buffer to land it; the bar reads "Find: ... (1 of 1, ..."
  a miss  "walrus", a word the file never carries; the bar reads
          "Find: walrus (no matches, ..."

The hit goes first, on the search a fresh editor opens empty; the
search keeps its query, so before the miss the prompt is cleared
with backspaces — outside the clock, the prompt redraws are not
the search. ESC restores between them. The median over the runs is
the answer.

  python3 bench/search.py ./vted [file] [runs]
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

CTRL_F = b'\x06'
CTRL_Q = b'\x11'
ESC = b'\x1b'
BACKSPACE = b'\x7f'
MISS = 'walrus'


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


def deepest_hit(path):
    """the seven-digit suffix of the last line: one match, the deepest
    row of the file — the walk needs the file make bench keeps for it"""
    with open(path, 'rb') as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(max(0, size - 64))
        tail = f.read()

    last = tail.rstrip(b'\n').split(b'\n')[-1]
    i = len(last)
    while i > 0 and 0x30 <= last[i - 1] <= 0x39:
        i -= 1

    return last[i:].decode() or None


def one_run(binary, file_arg, hit):
    """(first frame, the hit, the miss) in seconds, or None when a
    run went wrong"""
    pid, fd = spawn(binary, [file_arg])

    # the first painted frame: the load of the whole file is inside
    # it, and the searches must not start before the load is done
    first, _ = step(fd, b'', telltale=b'Ln 1/', quiet=0.3, timeout=15.0)

    if first is None:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return None

    # the hit: the search a fresh editor opens empty, the query
    # typed in one burst
    the_hit, _ = step(fd, CTRL_F + hit.encode(),
                      telltale=f'Find: {hit} (1 of 1,'.encode(),
                      timeout=15.0)

    # ESC restores the view the search started from; the next search
    # opens with the hit pre-filled, so the prompt is cleared first —
    # backspaces outside the clock, the prompt redraws are not the
    # search
    step(fd, ESC, quiet=0.05, timeout=1.0)
    step(fd, CTRL_F + BACKSPACE * (len(hit) + 4),
         telltale=b'Find: (', timeout=5.0)

    # the miss, the same burst
    the_miss = None
    if the_hit is not None:
        the_miss, _ = step(fd, MISS.encode(),
                           telltale=f'Find: {MISS} (no matches,'.encode(),
                           timeout=15.0)
        step(fd, ESC, quiet=0.05, timeout=1.0)

    try:
        os.write(fd, CTRL_Q)
    except OSError:
        pass  # the editor may have left already; its exit status says

    status = wait_exit(pid, fd)
    if status is None or not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return None
    if the_hit is None or the_miss is None:
        return None

    return first, the_hit, the_miss


def main() -> int:
    if len(sys.argv) < 2:
        print(f'usage: {sys.argv[0]} <path-to-vted> [file] [runs]',
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

    hit = deepest_hit(os.path.abspath(file_arg))
    if hit is None:
        print(f'bench: {file_arg} does not end its lines in digits — '
              'the walk needs the file make bench keeps',
              file=sys.stderr)
        return 2

    file_arg = os.path.abspath(file_arg)  # the child chdirs to /
    firsts = []
    hits = []
    misses = []

    for _ in range(runs):
        result = one_run(binary, file_arg, hit)
        if result is None:
            print('bench: a run went wrong', file=sys.stderr)
            return 1
        firsts.append(result[0])
        hits.append(result[1])
        misses.append(result[2])

    print(f'runs: {runs}, the file ending in "{hit}"')
    print(f'first frame: median {statistics.median(firsts) * 1e3:.1f} ms '
          f'(min {min(firsts) * 1e3:.1f}, max {max(firsts) * 1e3:.1f})')
    print(f'a hit, typed in one burst: median '
          f'{statistics.median(hits) * 1e3:.1f} ms '
          f'(min {min(hits) * 1e3:.1f}, max {max(hits) * 1e3:.1f})')
    print(f'a miss, typed in one burst: median '
          f'{statistics.median(misses) * 1e3:.1f} ms '
          f'(min {min(misses) * 1e3:.1f}, max {max(misses) * 1e3:.1f})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
