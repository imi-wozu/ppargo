use anyhow::Result;
use std::{
    env,
    path::{Path, PathBuf},
};

use crate::core::manifest::{Manifest, PackageManagerType};

pub struct PackageManager {
    pub manager_type: PackageManagerType,
    pub vcpkg_root: Option<PathBuf>,
    pub triplet: Option<String>,
    pub project_root: PathBuf,
}

impl PackageManager {
    pub fn new(project_root: &Path, manifest: &Manifest) -> Result<Self> {
        let (manager_type, vcpkg_root, triplet) = match manifest.features.package_manager {
            PackageManagerType::Vcpkg => {
                let root = manifest.features.vcpkg_root.clone()
                    .ok_or_else(|| anyhow::anyhow!("vcpkg_root must be specified in features when using vcpkg"))?;
                let trip = Self::detect_triplet();
                (PackageManagerType::Vcpkg, Some(root), Some(trip))
            }
            PackageManagerType::Ppargo => {
                (PackageManagerType::Ppargo, None, None)
            }
        };

        Ok(Self {
            manager_type,
            vcpkg_root,
            triplet,
            project_root: project_root.to_path_buf(),
        })
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

    pub fn install_dependencies(&self, _manifest: &Manifest) -> Result<()> {
        match self.manager_type {
            PackageManagerType::Vcpkg => {
                // vcpkg dependency installation would happen here
                Ok(())
            }
            PackageManagerType::Ppargo => {
                // ppargo dependency installation would happen here
                Ok(())
            }
        }
    }

    pub fn get_include_path(&self) -> Vec<PathBuf> {
        match self.manager_type {
            PackageManagerType::Vcpkg => {
                if let Some(vcpkg_root) = &self.vcpkg_root {
                    vec![vcpkg_root.join("installed").join("include")]
                } else {
                    vec![]
                }
            }
            PackageManagerType::Ppargo => vec![],
        }
    }

    pub fn get_lib_paths(&self) -> Vec<PathBuf> {
        match self.manager_type {
            PackageManagerType::Vcpkg => {
                if let (Some(vcpkg_root), Some(triplet)) = (&self.vcpkg_root, &self.triplet) {
                    vec![vcpkg_root.join("installed").join(triplet).join("lib")]
                } else {
                    vec![]
                }
            }
            PackageManagerType::Ppargo => vec![],
        }
    }
}
