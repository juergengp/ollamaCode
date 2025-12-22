# OlEg Examples

This document provides practical examples of using OlEg with tool calling.

## Basic Examples

### Example 1: File Search and Analysis

**Request:**
```bash
oleg "Find all shell scripts in this directory and show me which ones are executable"
```

**AI Workflow:**
1. Uses `Glob` to find all *.sh files
2. Uses `Bash` to run `ls -la *.sh` to check permissions
3. Analyzes output and presents findings

**Expected Tool Calls:**
```xml
<tool_calls>
<tool_call>
<tool_name>Glob</tool_name>
<parameters>
<pattern>*.sh</pattern>
<path>.</path>
</parameters>
</tool_call>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>ls -la *.sh</command>
<description>Check file permissions</description>
</parameters>
</tool_call>
</tool_calls>
```

---

### Example 2: Code Modification

**Request:**
```bash
oleg "In config.json, change the port from 8080 to 9090"
```

**AI Workflow:**
1. Uses `Read` to view config.json
2. Uses `Edit` to replace the port value
3. Confirms the change

**Expected Tool Calls:**
```xml
<tool_calls>
<tool_call>
<tool_name>Read</tool_name>
<parameters>
<file_path>./config.json</file_path>
</parameters>
</tool_call>
<tool_call>
<tool_name>Edit</tool_name>
<parameters>
<file_path>./config.json</file_path>
<old_string>"port": 8080</old_string>
<new_string>"port": 9090</new_string>
</parameters>
</tool_call>
</tool_calls>
```

---

### Example 3: System Diagnostics

**Request:**
```bash
oleg "Check disk usage and memory usage on this system"
```

**AI Workflow:**
1. Uses `Bash` to run `df -h`
2. Uses `Bash` to run `free -h`
3. Analyzes and explains the output

**Expected Tool Calls:**
```xml
<tool_calls>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>df -h</command>
<description>Check disk usage</description>
</parameters>
</tool_call>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>free -h</command>
<description>Check memory usage</description>
</parameters>
</tool_call>
</tool_calls>
```

---

## Advanced Examples

### Example 4: Multi-Step Project Setup

**Request:**
```bash
oleg "Create a basic Python project structure with src/ directory, main.py, requirements.txt, and README.md"
```

**AI Workflow:**
1. Uses `Bash` to create directories
2. Uses `Write` to create main.py with boilerplate
3. Uses `Write` to create requirements.txt
4. Uses `Write` to create README.md
5. Uses `Bash` to show the final structure

**Tool Calls:**
```xml
<tool_calls>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>mkdir -p src</command>
<description>Create source directory</description>
</parameters>
</tool_call>
<tool_call>
<tool_name>Write</tool_name>
<parameters>
<file_path>./src/main.py</file_path>
<content>#!/usr/bin/env python3
"""Main application entry point."""

def main():
    print("Hello, World!")

if __name__ == "__main__":
    main()
</content>
</parameters>
</tool_call>
<tool_call>
<tool_name>Write</tool_name>
<parameters>
<file_path>./requirements.txt</file_path>
<content># Project dependencies
# Add your dependencies here
</content>
</parameters>
</tool_call>
<tool_call>
<tool_name>Write</tool_name>
<parameters>
<file_path>./README.md</file_path>
<content># Project Name

## Description
Add project description here

## Installation
```bash
pip install -r requirements.txt
```

## Usage
```bash
python src/main.py
```
</content>
</parameters>
</tool_call>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>tree -L 2</command>
<description>Show project structure</description>
</parameters>
</tool_call>
</tool_calls>
```

---

### Example 5: Code Analysis and Refactoring

**Request:**
```bash
oleg "Find all functions in Python files that have more than 50 lines and create a report"
```

**AI Workflow:**
1. Uses `Glob` to find all .py files
2. Uses `Grep` to find function definitions
3. For each function, uses `Bash` to count lines between `def` and next `def`
4. Uses `Write` to create a report file

This demonstrates the AI's ability to plan and execute complex multi-step workflows.

---

### Example 6: Log Analysis

**Request:**
```bash
oleg "Find error messages in app.log from the last hour and summarize them"
```

**AI Workflow:**
1. Uses `Bash` to check if app.log exists
2. Uses `Grep` with pattern "ERROR" on app.log
3. Uses `Bash` to filter by timestamp (last hour)
4. Analyzes patterns and provides summary

---

## Interactive Mode Examples

### Session 1: Debugging

```
You> What Python version is installed?

AI: Let me check the Python version for you.

[Tool: Bash]
Command: python3 --version
Execute? (y/n/always): y
Output: Python 3.11.5

I can see you have Python 3.11.5 installed.

You> Check if pip is available

AI: Let me verify pip installation.

[Tool: Bash]
Command: pip --version
Execute? (y/n/always): y
Output: pip 23.2.1

Yes, pip is available at version 23.2.1.
```

---

### Session 2: Code Review

```
You> Read the main.py file and tell me if there are any issues

AI: Let me examine the main.py file.

[Tool: Read]
File: main.py
=== File Contents ===
[contents displayed]
====================

I've reviewed the code. Here are a few issues I noticed:
1. Missing error handling in the database connection
2. The PORT variable is hardcoded - should be in config
3. No input validation on user_input

Would you like me to fix these issues?

You> Yes, fix the hardcoded PORT issue

AI: I'll move the PORT to a config file and update main.py.

[Tool: Write]
File: config.py
✓ File written successfully

[Tool: Edit]
File: main.py
✓ File edited successfully

Done! I've created a config.py file and updated main.py to import the PORT from there.
```

---

## Tips for Best Results

### 1. Be Specific
❌ "Fix the code"
✅ "In server.py, fix the connection timeout issue by increasing the timeout to 30 seconds"

### 2. Break Down Complex Tasks
❌ "Refactor the entire project"
✅ "First find all TODO comments, then we'll prioritize which ones to address"

### 3. Use Auto-Approve for Trusted Tasks
```bash
oleg -a "Run the test suite and show me any failures"
```

### 4. Leverage Safe Mode
Keep safe mode on for exploring unfamiliar codebases or when learning.

### 5. Chain Commands in Interactive Mode
```
You> Find all Python files
You> Now check which ones have the main function
You> Create a list of entry points in entry_points.md
```

---

## Automation Examples

### CI/CD Integration

```bash
#!/bin/bash
# ci-check.sh

# Auto-approve mode for CI
oleg -a "Run pytest and if any tests fail, read the error logs and create a summary report in test-failures.md"

# Check if report was created
if [ -f "test-failures.md" ]; then
    cat test-failures.md
    exit 1
fi

exit 0
```

### Daily Reports

```bash
#!/bin/bash
# daily-report.sh

oleg -a "Check git log for today's commits, analyze changed files, and create a daily-summary.md report"
```

### Code Maintenance

```bash
#!/bin/bash
# find-todos.sh

oleg "Find all TODO, FIXME, and HACK comments in the codebase and create a prioritized todo-list.md"
```

---

## Network Ollama Examples

### Connect to Remote Ollama

```bash
# Set up remote connection
export OLLAMA_HOST=http://ai-server.company.com:11434

# Use as normal
oleg "Your request here"
```

### Team Shared Configuration

```bash
# Create shared config
cat > ~/.config/oleg/config << EOF
MODEL=llama3-70b
OLLAMA_HOST=http://shared-ai.local:11434
TEMPERATURE=0.5
SAFE_MODE=true
EOF

# Everyone on the team uses the same powerful model
oleg
```

---

## Debugging Tool Calls

If the AI isn't using tools correctly:

1. **Check the model** - Some models follow instructions better than others
2. **Lower temperature** - Try `temp 0.2` for more deterministic behavior
3. **Be more explicit** - "Use the Bash tool to run ls" instead of "show me files"
4. **Check logs** - Tool calls are displayed in real-time

---

## Contributing Examples

Have a great example? Submit a PR with your use case!

1. Fork the repository
2. Add your example to this file
3. Test it with OlEg
4. Submit a pull request

---

## Need Help?

- GitHub Issues: https://github.com/core-at/oleg/issues
- Email: support@core.at
- Documentation: README-v2.md

Happy coding with OlEg! 🚀
