# Team-SJP

COSC 4348 Project 3

## Overview

Team-SJP is a simple Unix-style shell written in C. The project supports
interactive command execution, built-in shell commands, background processes,
signal handling, I/O redirection, pipelines, and batch mode.

The final implementation for this project is in `shell_v2.c`.

## Team Members

- Sanskar Chaudhari
- Jacob Bryson
- Pearson Pollard

## Final Implementation

Use `shell_v2.c` as the final source file for compiling and testing the shell.

The older `shell.c` file is an earlier version and is not the final project
implementation.

## Features

- Execute normal Unix commands
- Built-in commands:
  - `cd`
  - `pwd`
  - `jobs`
  - `help`
  - `exit`
- Background execution using `&`
- Signal handling:
  - `Ctrl+C` terminates the foreground process
  - `Ctrl+Z` suspends the foreground process
- Input redirection using `<`
- Output redirection using `>`
- Single and multiple pipelines using `|`
- Batch mode using a command file
- Error handling for invalid commands and invalid redirection/pipeline syntax

## Requirements

- Linux or WSL
- GCC compiler
- Standard POSIX system libraries

## Compile Instructions

Compile the shell with:

```bash
gcc -Wall -Wextra -pedantic -o sjp-shell shell_v2.c
```

## Run Instructions

Run the shell interactively:

```bash
./sjp-shell
```

Run the shell in batch mode:

```bash
./sjp-shell commands.txt
```

## Example Commands

Basic commands:

```bash
pwd
help
echo hello
```

Background execution:

```bash
sleep 5 &
jobs
```

Input and output redirection:

```bash
echo hello > out.txt
cat < out.txt
```

Pipelines:

```bash
echo one two three | wc -w
echo one two three | cat | wc -w
```

Signal handling:

```bash
sleep 5
```

Then press `Ctrl+C` to terminate or `Ctrl+Z` to suspend the foreground process.

## Suggested Testing Checklist

- Verify normal command execution
- Verify all built-in commands
- Verify background execution with `&`
- Verify `Ctrl+C` and `Ctrl+Z`
- Verify input/output redirection
- Verify single and multiple pipelines
- Verify batch mode
- Verify invalid command and bad file/syntax handling

## Known Limitations

- Stretch-goal features such as command history, tab completion, environment
  variable support, and prompt customization are not implemented
- Full job-control commands such as `fg` and `bg` are not implemented

## Submission Notes

- Final implementation file: `shell_v2.c`
- Include this README with the final source code submission
- Test in Linux or WSL rather than plain PowerShell
