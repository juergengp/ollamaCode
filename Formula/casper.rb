class Casper < Formula
  desc "Agentic AI assistant powered by local LLMs"
  homepage "https://github.com/juergengp/ollamaCode"
  version "2.3.1"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/casper-arm64"
      sha256 "32253b5817eae538815bde58a0b0593921ca43684cfc91619d25b4cf7b462c56"
    else
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/casper-x86_64"
      sha256 "877afacaf5880c47b9283bbf8c8f4ac3cb81b5594f76f02dad2e04d15da6526d"
    end
  end

  depends_on :macos

  def install
    binary_name = Hardware::CPU.arm? ? "oleg-macos-arm64" : "oleg-macos-x86_64"
    bin.install Dir["*"].first => "casper"
  end

  test do
    assert_match "Casper version", shell_output("#{bin}/casper --version")
  end
end
