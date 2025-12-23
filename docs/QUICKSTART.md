# Casper Quick Start Guide

Get up and running with Casper in 5 minutes!

## Prerequisites

- macOS 10.15+ or Linux
- 8GB+ RAM recommended
- ~5GB disk space for models

---

## Step 1: Install Ollama (2 minutes)

### macOS

```bash
# Download and install
curl -fsSL https://ollama.ai/install.sh | sh
```

Or download from [ollama.ai](https://ollama.ai/download)

### Linux

```bash
curl -fsSL https://ollama.ai/install.sh | sh
```

### Verify Installation

```bash
ollama --version
# Output: ollama version 0.x.x
```

---

## Step 2: Start Ollama & Pull a Model (2 minutes)

```bash
# Start Ollama server (runs in background)
ollama serve &

# Pull a model (llama3.1 recommended for coding)
ollama pull llama3.1
```

**Other recommended models:**

| Model | Size | Download |
|-------|------|----------|
| llama3.1 | 4.7GB | `ollama pull llama3.1` |
| codellama | 3.8GB | `ollama pull codellama` |
| qwen2.5:14b | 9GB | `ollama pull qwen2.5:14b` |

---

## Step 3: Install Casper (1 minute)

### Option A: Pre-built Binary (macOS)

```bash
git clone https://github.com/juergengp/Casper.git
cd Casper
sudo cp bin/casper /usr/local/bin/
```

### Option B: Build from Source

```bash
git clone https://github.com/juergengp/Casper.git
cd Casper/cpp
mkdir build && cd build
cmake ..
make
sudo cp casper /usr/local/bin/
```

---

## Step 4: Run Casper!

### Interactive Mode

```bash
casper
```

You'll see:

```
   ____  _ _                       ____          _
  / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___
 | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \
 | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/
  \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|

Interactive CLI for Ollama - Version 2.0.2 (C++)
Type '/help' for commands, '/exit' to quit

You> _
```

### Try These Commands

```bash
# Ask a coding question
You> How do I read a JSON file in Python?

# Let the AI explore your codebase
You> What files are in the current directory?

# Ask for code review
You> Read main.py and suggest improvements

# Create a new file
You> Create a script that lists all TODO comments
```

---

## Quick Reference

### Essential Commands

| Command | What it does |
|---------|--------------|
| `/help` | Show all commands |
| `/models` | List available models |
| `/model` | Switch model interactively |
| `/config` | Show current settings |
| `/exit` | Exit Casper |

### Command Line Options

```bash
# Single prompt (non-interactive)
casper "Explain Docker containers"

# Use specific model
casper -m codellama "Write a Python function"

# Auto-approve all tools
casper -a

# Enable MCP servers
casper --mcp
```

---

## What's Next?

1. **Explore MCP** - Connect to external services
   ```bash
   casper --mcp
   You> /mcp tools
   ```

2. **Try different models** - Find your favorite
   ```bash
   You> /model
   ```

3. **Read the full docs**
   - [FEATURES.md](FEATURES.md) - All features explained
   - [MCP_SETUP.md](MCP_SETUP.md) - MCP configuration

---

## Troubleshooting

### "Failed to connect to Ollama"

```bash
# Make sure Ollama is running
ollama serve

# Check if it's listening
curl http://localhost:11434/api/tags
```

### "No models found"

```bash
# Pull a model first
ollama pull llama3.1
```

### "Command not allowed in safe mode"

```bash
# Temporarily disable safe mode (use with caution)
You> /safe off
```

---

## Need Help?

- [GitHub Issues](https://github.com/juergengp/Casper/issues)
- [Full Documentation](https://github.com/juergengp/Casper#documentation)
