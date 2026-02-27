
# COMP 304 - Mustafa AKDENIZ

**Student Name:** Mustafa Akdeniz
**GitHub Repository:** https://github.com/makdenizz/comp304 


## Description
Shell-ish is an interactive, Unix-style command-line interpreter (shell) written in C. It reads commands from the standard input, parses them, and executes them either as background or foreground processes. It supports I/O redirection, piping, and includes several custom built-in commands.

## Features Implemented
* **Part I - Basic Execution:** Executes standard system commands using the `execv()` system call by resolving their absolute paths. Supports background execution when `&` is appended.
* **Part II - I/O Redirection & Piping:** Supports input redirection (`<`), output redirection with truncation (`>`), output redirection with appending (`>>`), and recursive piping (`|`) connecting the standard output of one process to the standard input of another.
* **Part III - Built-in Commands:**
  * `cut`: A built-in version of the Unix `cut` command. Supports custom delimiters (`-d`) and specific field extraction (`-f`).
  * `chatroom`: A terminal-based group chat application utilizing named pipes (FIFOs) under `/tmp/chatroom-<roomname>/`. Users can send and receive messages in real-time.
  * `masktext` (Custom Command): A Python-based utility integrated into the shell.

## Custom Command: `masktext`
`masktext` is a privacy-focused text processing tool designed to detect and mask Personally Identifiable Information (PII) directly from the terminal. 

**How it works:**
It takes a string argument from the user, processes it using Regular Expressions (Regex), and masks email addresses and phone numbers (10-11 digits) with asterisks (`***`). This tool is especially useful for quickly anonymizing sensitive data streams before saving them to logs or files.

**Usage Example:**
```bash
shellish$ masktext "Bana ulasmak icin makdeniz22@ku.edu.tr adresine yaz veya 05533010726 numarasini ara."
Bana ulasmak icin ***@***.*** adresine yaz veya ***-***-**** numarasini ara.