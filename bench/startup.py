#!/usr/bin/env python3
"""bench/startup.py — time the editor coming up and leaving.

The editor is spawned on a pseudo terminal the way packaging/
smoke-test.py does it, and two clocks are taken per run: the time
from the process starting to the first painted frame — told from
the setup escapes before it by waiting for the bar of the opening
view, or the welcome banner of an empty buffer, so the open of the
file is inside the number — and the time to a clean exit after one
Ctrl-Q. The median of the runs is the answer; the terminal is 24x80
so the frame is small and the fixed costs dominate, which is what
startup is.

  python3 bench/startup.py ./vted [file] [runs]
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

CTRL_Q = b'\x11'


def spawn(binary, args):
    pid, fd = pty.fork()
    if pid == 0:  # child: run the editor, far from anything it might touch
        os.chdir('/')
        os.execv(binary, ['vted'] + args)
        os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    return pid, fd


def wait_exit(pid, fd, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            os.close(fd)
            return status
        time.sleep(0.001)  # 1 ms: the exit clock keeps its resolution
    return None


def one_run(binary, args):
    """(first frame, clean exit) in seconds, or None when it went wrong"""
    # the first painted frame is told from the setup escapes before
    # it: the bar of the opening view ("Ln 1/...") only arrives once
    # the file is loaded and laid out behind it, and an empty buffer
    # paints its welcome banner instead — the first of the two the
    # file on the command line decides between
    telltale = b'Ln 1/' if args else b'vted -- version'

    start = time.perf_counter()
    pid, fd = spawn(binary, args)

    first_frame = None
    seen = b''
    deadline = time.time() + 2.0
    while time.perf_counter() - start < deadline and first_frame is None:
        ready, _, _ = select.select([fd], [], [], 0.01)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        seen += chunk
        if telltale in seen:  # the bar is drawn: the frame is painted
            first_frame = time.perf_counter() - start

    if first_frame is None:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return None

    os.write(fd, CTRL_Q)
    status = wait_exit(pid, fd)
    if status is None:
        return None
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return None

    exit_at = time.perf_counter() - start
    return first_frame, exit_at


def main() -> int:
    if len(sys.argv) < 2:
        print(f'usage: {sys.argv[0]} <path-to-vted> [file] [runs]',
              file=sys.stderr)
        return 2

    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'bench: {binary} is not executable', file=sys.stderr)
        return 2

    # the child chdirs to / before it execs, so the file is opened by
    # its absolute path; none opens an empty buffer
    file_args = sys.argv[2:3]
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 15

    frames = []
    exits = []
    for _ in range(runs):
        result = one_run(binary, file_args)
        if result is None:
            print('bench: a run went wrong', file=sys.stderr)
            return 1
        frames.append(result[0])
        exits.append(result[1])

    print(f'runs: {runs}')
    print(f'first frame: median {statistics.median(frames) * 1e3:.1f} ms '
          f'(min {min(frames) * 1e3:.1f}, max {max(frames) * 1e3:.1f})')
    print(f'up and out:   median {statistics.median(exits) * 1e3:.1f} ms '
          f'(min {min(exits) * 1e3:.1f}, max {max(exits) * 1e3:.1f})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
