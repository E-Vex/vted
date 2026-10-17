#!/usr/bin/env python3
# smoke-test.py — run vted in a pseudo terminal, quit it with Ctrl-Q,
# and require a clean exit. The editor refuses to run without a real
# terminal, which makes a plain shell test awkward; python's pty
# module gives us exactly the terminal we need.
#
#   python3 packaging/smoke-test.py ./vted
#   python3 packaging/smoke-test.py dist/vted_0.2.0_amd64.deb-extracted
import os
import pty
import select
import struct
import sys
import termios
import fcntl
import time

CTRL_Q = b'\x11'


def main() -> int:
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} <path-to-vted>', file=sys.stderr)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'smoke: {binary} is not executable', file=sys.stderr)
        return 2

    pid, fd = pty.fork()
    if pid == 0:  # child: run the editor on a small scratch file
        os.chdir('/')
        with open('/tmp/vted-smoke.txt', 'w') as f:
            f.write('vted smoke test\n')
        os.execv(binary, ['vted', '/tmp/vted-smoke.txt'])
        os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))

    time.sleep(0.3)
    for _ in range(3):  # Ctrl-Q three times: modified buffer guard
        os.write(fd, CTRL_Q)
        time.sleep(0.1)

    status = None
    deadline = time.time() + 10
    while time.time() < deadline:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            break
        time.sleep(0.05)
    else:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        print('smoke: FAIL - the editor did not quit within 10s', file=sys.stderr)
        return 1
    os.close(fd)

    if os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
        print('smoke: OK - started, opened a file and quit cleanly')
        return 0
    print(f'smoke: FAIL - exit status {status:#x}', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
