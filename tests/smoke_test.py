import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run(command):
    return subprocess.run(command, capture_output=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--eciyuan', type=Path)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    launcher = root / 'dist' / 'EciyuanDesktopCompanion.exe'
    assert run([str(launcher), '--self-test']).returncode == 0
    for source in ('capture_hook.cpp', 'anchor.cpp'):
        content = (root / 'src' / source).read_text(encoding='utf-8')
        assert 'eciyuan_cmd' not in content
        assert 'eciyuan_dump_frame' not in content
        assert 'SendInput' not in content
    powershell = str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe')
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        manager = folder / 'manage.ps1'
        manager.write_text((root / 'src/manage.ps1').read_text(encoding='utf-8'), encoding='utf-8-sig')
        fake = folder / 'Eciyuan.exe'
        fake.write_bytes(b'MZunsupported-version')
        original = folder / 'zlib1.dll'
        original.write_bytes(b'original-must-survive')
        invocation = [powershell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(manager)]
        result = run(invocation + ['-Action', 'Enable', '-EciyuanExe', str(fake)])
        assert result.returncode == 1
        assert original.read_bytes() == b'original-must-survive'
        assert not (folder / 'zlib1_real.dll').exists()
        report = (folder / 'result.txt').read_text(encoding='utf-8-sig')
        assert '不支持此主程序' in report, report
        if arguments.eciyuan:
            result = run(invocation + ['-Action', 'Inspect', '-EciyuanExe', str(arguments.eciyuan)])
            assert result.returncode == 0, result.stderr.decode(errors='replace')
            assert '兼容性校验通过' in (folder / 'result.txt').read_text(encoding='utf-8-sig')
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    user.FindWindowW.restype = ctypes.c_void_p
    user.IsWindowVisible.argtypes = [ctypes.c_void_p]
    user.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    name = 'EciyuanDesktopCompanionAnchor'
    if user.FindWindowW(name, None):
        print('SKIP: an existing user sharing window is active; left untouched')
    else:
        anchor = subprocess.Popen([str(root / 'build' / 'Valorant.exe')])
        try:
            deadline = time.monotonic() + 8
            window = None
            while time.monotonic() < deadline:
                window = user.FindWindowW(name, None)
                if window:
                    break
                time.sleep(0.05)
            assert window
            owner = ctypes.c_ulong()
            user.GetWindowThreadProcessId(window, ctypes.byref(owner))
            assert owner.value == anchor.pid
            assert user.IsWindowVisible(window)
            user.SendMessageW(window, 0x8001, 0, 0)
            assert not user.IsWindowVisible(window)
            assert anchor.poll() is None
            assert run([str(root / 'build' / 'Valorant.exe')]).returncode == 0
            assert user.IsWindowVisible(window)
            user.SendMessageW(window, 0x0010, 0, 0)
            assert not user.IsWindowVisible(window)
            assert anchor.poll() is None
        finally:
            anchor.terminate()
            anchor.wait(timeout=5)
    print('PASS: embedded payloads, fail-closed version check, backup preservation, pause/resume without process exit')


if __name__ == '__main__':
    main()
