class Oleg < Formula
  desc "Local AI coding assistant powered by Ollama - Claude Code experience, zero cloud"
  homepage "https://github.com/juergengp/OlEg"
  version "2.3.1"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/OlEg/raw/main/bin/oleg-arm64"
      sha256 "f1670d21387285749fad3781bb00b27a72647d17acc42fdcd086f93db1429da2"
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
