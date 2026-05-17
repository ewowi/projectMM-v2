#!/usr/bin/env python3
"""Run cppcheck over src/ with warning+style+performance+portability checks.

Suppressions:
  missingInclude*    — PlatformIO lib_deps not on the include path here.
  returnDanglingLifetime in ModuleManager.cpp — false positive: raw pointer
      captured before unique_ptr moves into vector; pointer stays valid.
  knownConditionTrueFalse — PC stubs return 0; on ESP32 these are runtime values.
  useStlAlgorithm    — intentional raw loops (readability + constexpr context).
  uselessOverride    — category() overrides are explicit documentation.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.exit(subprocess.call(
    [
        "cppcheck",
        "--enable=warning,style,performance,portability",
        "--suppress=missingIncludeSystem",
        "--suppress=missingInclude",
        "--suppress=returnDanglingLifetime:src/core/ModuleManager.cpp",
        "--suppress=knownConditionTrueFalse",
        "--suppress=useStlAlgorithm",
        "--suppress=uselessOverride",
        "--suppress=cstyleCast",
        "--suppress=constVariableReference",
        "--suppress=constVariablePointer",
        "--suppress=constParameterPointer",
        "--suppress=functionStatic",
        "--suppress=normalCheckLevelMaxBranches",
        "--error-exitcode=1",
        "-q",
        "src/",
    ],
    cwd=ROOT,
))
