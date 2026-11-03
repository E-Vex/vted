#!/usr/bin/env python3
# smoke-test.py — run vted in a pseudo terminal, quit it with Ctrl-Q,
# and require a clean exit. The editor refuses to run without a real
# terminal, which makes a plain shell test awkward; python's pty
# module gives us exactly the terminal we need.
#
# A second run walks the word completion the way a hand would: two
# files open as buffers, a half word typed into the second, Alt-/
# listing the words the first one offers — each entry of the menu
# naming the buffer it came from — another Alt-/ stepping down the
# list the way dabbrev-expand does in Emacs, Enter taking the word
# and Ctrl-S saving it. The file left on disk is the proof the whole
# interactive path worked.
#
#   python3 packaging/smoke-test.py ./vted
#   python3 packaging/smoke-test.py dist/vted_0.3.2_amd64.deb-extracted
import os
import pty
import select
import struct
import sys
import termios
import fcntl
import time

CTRL_Q = b'\x11'
CTRL_S = b'\x13'
ALT_DOT = b'\x1b.'    # Alt-.: the buffer after the current one
ALT_SLASH = b'\x1b/'  # Alt-/: complete the word left of the cursor

SMOKE = '/tmp/vted-smoke.txt'
WORDS = '/tmp/vted-smoke-words.txt'  # the buffer the answers come from
NOTE = '/tmp/vted-smoke-note.txt'    # the buffer the word is typed into


def spawn(binary, args):
    """the editor on a pseudo terminal of 24 rows and 80 columns"""
    pid, fd = pty.fork()
    if pid == 0:  # child: run the editor, far from anything it might touch
        os.chdir('/')
        os.execv(binary, ['vted'] + args)
        os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    return pid, fd


def drive(fd, keys, out, pause=0.25):
    """one round of keystrokes, then whatever frames they drew,
    drained into out until pause seconds passed without them"""
    try:
        os.write(fd, keys)
    except OSError:
        pass  # the editor may have left already; its exit status says

    deadline = time.time() + pause
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            return
        if not chunk:
            return
        out.append(chunk)


def wait_exit(pid, fd, timeout=10):
    """the editor's exit status once it left on its own, or None when
    it had to be killed after timeout seconds"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            os.close(fd)
            return status
        time.sleep(0.05)

    os.kill(pid, 9)
    os.waitpid(pid, 0)
    os.close(fd)
    return None


def fail(message):
    print(f'smoke: FAIL - {message}', file=sys.stderr)
    return 1


def quit_smoke(binary):
    """the plain run: start, open a file, quit cleanly"""
    with open(SMOKE, 'w') as f:
        f.write('vted smoke test\n')

    pid, fd = spawn(binary, [SMOKE])
    time.sleep(0.3)
    for _ in range(3):  # Ctrl-Q three times: modified buffer guard
        drive(fd, CTRL_Q, [], pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
        print('smoke: OK - started, opened a file and quit cleanly')
        return 0
    return fail(f'exit status {status:#x}')


def completion_smoke(binary):
    """the interactive run: a word completed from another buffer,
    stepped onto and saved"""
    with open(WORDS, 'w') as f:
        f.write('alpha alphabet\n')
    with open(NOTE, 'w') as f:
        f.write('\nx\n')  # an empty row to type on, one word off the way

    pid, fd = spawn(binary, [WORDS, NOTE])
    out = []
    time.sleep(0.3)       # the first frame, in the words buffer
    drive(fd, ALT_DOT, out)   # Alt-.: into the note buffer
    drive(fd, b'al', out)     # the half word to complete
    drive(fd, ALT_SLASH, out)  # Alt-/: the menu, its words named
    drive(fd, ALT_SLASH, out)  # Alt-/ again: step down the list
    drive(fd, b'\r', out)     # Enter: take the stepped-to word
    drive(fd, CTRL_S, out)    # Ctrl-S: save the note
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the menu said where each answer came from: the words of the
    # other buffer, drawn with its name behind them
    for word in (b'alpha', b'alphabet'):
        if word + b' - ' + WORDS.encode() not in drew:
            return fail(f"the menu did not draw '{word.decode()}' "
                        f"named after the words buffer")

    # the second Alt-/ stepped onto the far word: Enter took it and
    # the saved note carries it
    with open(NOTE) as f:
        note = f.read()
    if note != 'alphabet\nx\n':
        return fail(f'the note reads {note!r}, '
                    'not the word the step landed on')

    print('smoke: OK - completed a word from another buffer, '
          'stepped down and saved')
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} <path-to-vted>', file=sys.stderr)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'smoke: {binary} is not executable', file=sys.stderr)
        return 2

    return quit_smoke(binary) or completion_smoke(binary)


if __name__ == '__main__':
    sys.exit(main())
