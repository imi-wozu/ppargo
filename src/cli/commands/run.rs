use std::{result::Result, path::{Path, PathBuf}, fs, process::Command};

use crate::core::manifest::load_manifest_from_cwd;


pub fn execute() {
    // build before running
    println!("Building before running...");
    super::build::execute();

    let manifest = match load_manifest_from_cwd() {
        Ok(m) => m,
        Err(e) => {
            eprintln!("{}", e);
            return;
        }
    };

    let exe = Path::new("target")
        .join("debug")
        .join(&format!("{}.exe", manifest.package.name));
    if !exe.exists() {
        eprintln!("Executable not found. Build may have failed.");
        return;
    }

    println!("Running `{}`...\n", manifest.package.name);
    let status = Command::new(exe).status();
    match status {
        Ok(s) => {
            if !s.success() {
                eprintln!("Program exited with non-zero status: {:?}", s.code());
            }
        }
        Err(e) => {
            eprintln!("Failed to run executable: {}", e);
        }
    }
}
