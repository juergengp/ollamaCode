class Oleg < Formula
  desc "Local AI coding assistant powered by Ollama - Claude Code experience, zero cloud"
  homepage "https://github.com/juergengp/OlEg"
  version "2.3.0"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/OlEg/raw/main/bin/oleg-arm64"
      sha256 "0ee2c07aa213b6f32d215f3ae5c860e8314cb758fafc1835871361500a291ab2"
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
