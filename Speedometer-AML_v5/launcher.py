"""
Speedometer — desktop launcher
Wraps index.html/script.js/styles.css in a native window using pywebview,
so it can be packaged into a standalone .exe with PyInstaller.
"""
import os
import sys
import webview


def resource_path(relative_path: str) -> str:
    """Get absolute path to a resource, works both for normal run
    and for PyInstaller's onefile bundle (which extracts to a temp
    folder at runtime, exposed via sys._MEIPASS)."""
    base_path = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base_path, relative_path)


def main() -> None:
    index_path = resource_path("index.html")
    webview.create_window("Speedometer", index_path, width=1000, height=700)
    webview.start()


if __name__ == "__main__":
    main()