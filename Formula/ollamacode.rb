class Ollamacode < Formula
  desc "Local AI coding assistant powered by Ollama - Claude Code experience, zero cloud"
  homepage "https://github.com/juergengp/ollamaCode"
  version "2.1.0"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/ollamacode-arm64"
      sha256 "d6a8c28601bd08a0db972d63f06841196c54baaed614a4cdece715a4758c4311"
    else
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/ollamacode-x86_64"
      sha256 "877afacaf5880c47b9283bbf8c8f4ac3cb81b5594f76f02dad2e04d15da6526d"
    end
  end

  depends_on :macos

  def install
    binary_name = Hardware::CPU.arm? ? "ollamacode-macos-arm64" : "ollamacode-macos-x86_64"
    bin.install Dir["*"].first => "ollamacode"
  end

  test do
    assert_match "ollamaCode version", shell_output("#{bin}/ollamacode --version")
  end
end
