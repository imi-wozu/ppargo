use anyhow::{Result, Context, bail};
use std::{
    env,
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use crate::core::manifest::Manifest;

pub struct PackageManager {
    vcpkg_root: PathBuf,
    triplet: String,
    project_root: PathBuf,
}

impl PackageManager {
    pub fn new(project_root: &Path) -> Result<Self> {
        let vcpkg_root = Self::find_vcpkg()?;
        let triplet = Self::detect_triplet();
        
        Ok(Self {
            vcpkg_root,
            triplet,
            project_root: project_root.to_path_buf(),
        })
    }

    fn find_vcpkg() -> Result<PathBuf> {
        // Try environment variable
        if let Ok(root) = env::var("VCPKG_ROOT") {
            let path = PathBuf::from(&root);
            if path.exists() && path.join("vcpkg").exists() {
                return Ok(path);
            }
        }

        // Try common locations
        let home = dirs::home_dir()
            .ok_or_else(|| anyhow::anyhow!("Failed to get home directory"))?;
        
        let common_paths = vec![
            home.join("vcpkg"),
            home.join(".vcpkg"),
            PathBuf::from("/usr/local/vcpkg"),
            PathBuf::from("C:\\vcpkg"),
        ];

        for path in common_paths {
            if path.exists() && path.join("vcpkg").exists() {
                return Ok(path);
            }
        }

        bail!(
            "vcpkg not found. Please install vcpkg and set VCPKG_ROOT environment variable.\n  \
             Install: git clone https://github.com/microsoft/vcpkg.git ~/vcpkg\n  \
             Then run: cd ~/vcpkg && ./bootstrap-vcpkg.sh"
        )
    }

    fn detect_triplet() -> String {
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
                eprintln!("Warning: Unknown platform {}-{}, defaulting to x64-linux", os, arch);
                "x64-linux"
            }
        }.to_string()
    }

    pub fn install_dependencies(&self, manifest: &Manifest) -> Result<()> {
        Ok(())
    }

    pub fn get_include_path(&self) -> Vec<PathBuf> {
        vec![]
    }

    pub fn get_lib_paths(&self) -> Vec<PathBuf> {
        vec![]
    }
}
