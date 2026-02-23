from __future__ import annotations

import glob
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup
from setuptools.command.build_ext import build_ext as _build_ext


# -----------------------------------------------------------------------------
# Version metadata from git
# -----------------------------------------------------------------------------
@dataclass(frozen=True)
class BuildInfo:
    git_hash: str
    compile_time: str


_GIT_HASH_RE = re.compile(r"^[0-9a-f]+$")
_GIT_DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}.*$")


def _run_git(*args: str) -> str:
    return subprocess.check_output(["git", *args], stderr=subprocess.STDOUT).decode().strip()


def get_build_info() -> BuildInfo:
    try:
        shash = _run_git("rev-parse", "--short", "HEAD")
        sdate = _run_git("log", "-1", "--pretty=%ci")
        if _GIT_HASH_RE.match(shash) and _GIT_DATE_RE.match(sdate):
            return BuildInfo(git_hash=shash, compile_time=sdate)
    except (OSError, subprocess.CalledProcessError):
        pass
    return BuildInfo(git_hash="Unknown", compile_time="Unknown")


BUILD_INFO = get_build_info()


# -----------------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------------
BASE_CXX = ["-std=c++17", "-O3", "-fvisibility=hidden"]
UNIX_CXX = BASE_CXX + ["-Wno-unknown-pragmas"]
MSVC_CXX = ["-std=c++17", "-O2"]

BUILD_ARGS: dict[str, list[str]] = {
    "msvc": MSVC_CXX,
    "gcc": UNIX_CXX,
    "unix": UNIX_CXX,
}


class build_ext(_build_ext):
    def build_extensions(self) -> None:
        compiler = self.compiler.compiler_type
        extra = BUILD_ARGS.get(compiler, UNIX_CXX)
        for ext in self.extensions:
            ext.extra_compile_args = list(extra)
        super().build_extensions()


# -----------------------------------------------------------------------------
# Extension module
# -----------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent

# Put the extension inside the package so `import richdem._richdem` works.
EXT_NAME = "richdem._richdem"

SRC_FILES = ["src/pywrapper.cpp"] + glob.glob(
    str(ROOT / "lib" / "richdem" / "src" / "**" / "*.cpp"),
    recursive=True,
)

ext_modules = [
    Pybind11Extension(
        EXT_NAME,
        SRC_FILES,
        include_dirs=[str(ROOT / "lib" / "richdem" / "include")],
        define_macros=[
            ("DOCTEST_CONFIG_DISABLE", None),
            ("RICHDEM_COMPILE_TIME", BUILD_INFO.compile_time),
            ("RICHDEM_GIT_HASH", BUILD_INFO.git_hash),
            ("_USE_MATH_DEFINES", None),
        ],
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
