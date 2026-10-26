# Working instructions

## Execution & Permission Policy

- **Default Authority:** Full, unrestricted autonomy is granted for all standard operations (file creation, modification, code execution, and local configuration). Execute tasks immediately without requesting prior confirmation.
- **Explicit Approval Required:** You must halt execution and request user confirmation ONLY for the following high-risk operations:
  1. Production or external deployments.
  2. Extreme, large-scale merges or mass deletions.

## Clear writing and optional ASD-STE100

- Do not require `asd-ste100` for every task or response.
- Use it for an explicit ASD-STE100 request or English operating, installation,
  maintenance, or troubleshooting documentation.
- Otherwise use short sentences, explicit actions, and consistent terminology.
- Preserve the user's language, conditions, exceptions, technical meaning,
  source code, commands, paths, URLs, identifiers, UI labels, units, and values.
- Follow applicable project skills and instructions without replacing the task
  with a language review.

## User instruction recorded 2026-10-26: agents must not run builds

- 에이전트는 빌드를 직접 수행하지 않는다.
- Do not run firmware, native simulator, host-test executable, or application
  builds, including through another script or agent.
- Do not invoke compilers, CMake, Ninja, `idf.py` build/configuration actions,
  or build wrapper scripts. Do not run a test runner that compiles C/C++.
- Source inspection, editing, and Python/JavaScript checks that do not compile
  or trigger a build are allowed.
- Give the user the exact build command when needed. The user runs the build;
  diagnose the returned output without starting another build yourself.
- Do not claim successful compilation, linking, or device verification based
  only on source inspection or non-compiling checks.
