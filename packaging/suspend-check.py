#!/usr/bin/env python3
# suspend-check.py — drive vted through a real interactive shell on a
# pseudo terminal and verify the Alt-z suspend round trip the way a
# hand would: Alt-z stops the editor, the shell gets a cooked
# terminal and reports the stopped job, fg brings it back, and the
# editor repaints and keeps editing.
#
# The editor cannot be driven bare (pty.fork leaves it in an orphaned
# process group, where POSIX discards SIGTSTP — nobody would exist to
# continue it), so bash runs the job control a terminal session has.
#
#   python3 packaging/suspend-check.py ./vted
import os
import pty
import select
import signal
import struct
import sys
import termios
import fcntl
import time

ALT_Z = b'\x1bz'
CTRL_Q = b'\x11'


def spawn(binary):
    """bash in charge of a pseudo terminal of 24 rows and 80 columns"""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir('/')
        os.execv('/bin/bash', ['bash', '--noprofile', '--norc', '-i'])
        os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    return pid, fd


def drain(fd, pause=0.6):
    out = b''
    deadline = time.time() + pause
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            out += os.read(fd, 65536)
        except OSError:
            break
    return out


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else './vted'
    # the child walks far away before exec: the path must survive that
    binary = os.path.abspath(binary)

    pid, fd = spawn(binary)
    drain(fd, 0.8)  # the bash prompt

    # the editor takes over the terminal
    os.write(fd, binary.encode() + b'\r')
    frame = drain(fd, 1.0)
    assert b'vted -- version' in frame, f'editor did not start: {frame[-120:]!r}'
    print('OK - editor started under a job-control shell')

    # Alt-z: the editor stops and the shell gets the terminal back
    os.write(fd, ALT_Z)
    stopped = drain(fd, 1.0)
    assert b'Stopped' in stopped, f'shell saw no stopped job: {stopped[-120:]!r}'
    print('OK - Alt-z stopped the editor and the shell reported the job')

    # fg: the editor comes back and repaints the whole frame
    os.write(fd, b'fg\r')
    resumed = drain(fd, 1.0)
    assert b'vted -- version' in resumed, f'no frame after fg: {resumed[-120:]!r}'
    print('OK - fg resumed the editor and it repainted')

    # it keeps editing afterwards
    os.write(fd, b'hi')
    typed = drain(fd, 0.5)
    assert b'hi' in typed, f'typing broken after resume: {typed[-120:]!r}'
    print('OK - typing works after the round trip')

    # and a second suspend still lands
    os.write(fd, ALT_Z)
    stopped = drain(fd, 1.0)
    assert b'Stopped' in stopped, f'second suspend failed: {stopped[-120:]!r}'
    os.write(fd, b'fg\r')
    resumed = drain(fd, 1.0)
    # the buffer holds typed text now, so the frame has no banner:
    # the status bar is the repaint's proof
    assert b'Ln 1/1' in resumed, f'second fg failed: {resumed[-120:]!r}'
    print('OK - a second suspend round trip works too')

    # leave cleanly: the buffer holds typed text, so quitting asks
    # for confirmations on the way out
    quitting = b''
    for _ in range(4):
        os.write(fd, CTRL_Q)
        time.sleep(0.2)
        quitting += drain(fd, 0.2)

    # the editor's exit hands the shell its prompt back; the prompt
    # may have landed inside any of the drains above
    assert b'$' in quitting, f'shell prompt never came back: {quitting[-120:]!r}'
    os.write(fd, b'exit\r')

    deadline = time.time() + 5
    while time.time() < deadline:
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, \
                f'shell left uncleanly: {status:#x}'
            print('OK - clean exit after the round trips')
            os.close(fd)
            return 0
        time.sleep(0.05)

    os.kill(pid, signal.SIGKILL)
    raise AssertionError('shell never left on Ctrl-Q')


if __name__ == '__main__':
    sys.exit(main())
