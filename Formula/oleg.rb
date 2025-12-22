class Oleg < Formula
  desc "Local AI coding assistant powered by Ollama - Claude Code experience, zero cloud"
  homepage "https://github.com/juergengp/OlEg"
  version "2.3.0"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/OlEg/raw/main/bin/oleg-arm64"
      sha256 "4012c3cbee14a1651ee2024d74ad4a30868cf044f9f9895a442ee54304760d85"
    else
      url "https://github.com/juergengp/OlEg/raw/main/bin/oleg-x86_64"
      sha256 "877afacaf5880c47b9283bbf8c8f4ac3cb81b5594f76f02dad2e04d15da6526d"
    end
  end

  depends_on :macos

  def install
    binary_name = Hardware::CPU.arm? ? "oleg-macos-arm64" : "oleg-macos-x86_64"
    bin.install Dir["*"].first => "oleg"
  end

  test do
    assert_match "OlEg version", shell_output("#{bin}/oleg --version")
  end
end
