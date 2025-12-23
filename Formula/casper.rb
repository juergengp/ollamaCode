class Casper < Formula
  desc "Agentic AI assistant powered by local LLMs"
  homepage "https://github.com/juergengp/ollamaCode"
  version "2.3.1"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/juergengp/ollamaCode/raw/main/bin/casper-arm64"
      sha256 "7f90745e8ddb7bf8466f5da5d5745541f155d10eca6cdb3b5bb07ae21f544509"
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
