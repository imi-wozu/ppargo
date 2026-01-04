use std::env;

use anyhow::Error;
use colored::*;

pub fn print_error(e: Error) {
    eprintln!("{} {}", "error:".red().bold(), e);
}

pub(crate) fn detect_triplet() -> String {
    let os = env::consts::OS;
    let arch = env::consts::ARCH;

    match (os, arch) {
        ("linux", "x86_64") => "x64-linux",
        ("linux", "aarch64") => "arm64-linux",
        ("macos", "x86_64") => "x64-osx",
        ("macos", "aarch64") => "arm64-osx",
        ("windows", "x86_64") => "x64-windows",
        ("windows", "aarch64") => "arm64-windows",
        _ => {
            eprintln!(
                "Warning: Unknown platform {}-{}, defaulting to x64-linux",
                os, arch
            );
            "x64-linux"
        }
    }
    .to_string()
}
