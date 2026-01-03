use anyhow::{Context, Ok, Result, bail};
use reqwest::Version;
use serde::{Deserialize, Serialize};
use std::{
    env, fs,
    path::{Path, PathBuf},
    process::Command,
};

use crate::core::{manifest::PackageManagerType};
use super::vcpkg;

pub struct PackageManager {
    pub manager_type: PackageManagerType,
    pub vcpkg: Option<vcpkg::VcpkgManifest>,
    pub triplet: Option<String>,
}

// // #[derive(Debug, Serialize, Deserialize)]
// // struct VcpkgManifest {
// //     #[serde(rename = "$schema")]
// //     schema: String,
// //     name: String,
// //     version: String,
// //     dependencies: Vec<VcpkgDependency>,
// //     #[serde(rename = "builtin-baseline")]
// //     builtin_baseline: String,
// // }

// // #[derive(Debug, Serialize, Deserialize)]
// // #[serde(untagged)]
// // enum VcpkgDependency {
// //     Simple(String),
// //     Detailed {
// //         name: String,
// //         #[serde(skip_serializing_if = "Option::is_none")]
// //         version: Option<String>,
// //         #[serde(skip_serializing_if = "Option::is_none")]
// //         features: Option<Vec<String>>,
// //     },
// // }

impl PackageManager {
    pub fn new() -> Result<Self> {

        let (manager_type, vcpkg) = match crate::core::get_manifest().features.package_manager{
            PackageManagerType::Vcpkg => {
                let root = crate::core::get_manifest()
                    .features
                    .vcpkg_root
                    .clone()
                    .ok_or_else(|| {
                        anyhow::anyhow!("vcpkg_root must be specified in features when using vcpkg")
                    })?;

                let vcpkg = vcpkg::VcpkgManifest { root };
                (PackageManagerType::Vcpkg, Some(vcpkg))
            }
            PackageManagerType::Ppargo => (PackageManagerType::Ppargo, None),
        };

        let triplet = Some(Self::detect_triplet());

        Ok(Self {
            manager_type,
            vcpkg,
            triplet,
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
                eprintln!(
                    "Warning: Unknown platform {}-{}, defaulting to x64-linux",
                    os, arch
                );
                "x64-linux"
            }
        }
        .to_string()
    }
}
//     fn vcpkg_exe(&self) -> Result<PathBuf> {
//         let vcpkg_root = self
//             .vcpkg_root
//             .as_ref()
//             .ok_or_else(|| anyhow::anyhow!("vcpkg_root not set"))?;

//         let exe = if cfg!(windows) {
//             vcpkg_root.join("vcpkg.exe")
//         } else {
//             vcpkg_root.join("vcpkg")
//         };

//         if !exe.exists() {
//             bail!("vcpkg executable not found at {}", exe.display());
//         }

//         Ok(exe)
//     }

//     pub fn install_dependencies(&self) -> Result<()> {
//         if !self.project.manifest.features.packages {
//             return Ok(());
//         }

//         if self.project.manifest.dependencies.is_empty() {
//             return Ok(());
//         }

//         match self.manager_type {
//             PackageManagerType::Vcpkg => {
//                 self.install_vcpkg_dependencies(self.project.manifest)?;
//             }
//             PackageManagerType::Ppargo => {
//                 //ppargo dependency installation
//             }
//         }

//         Ok(())
//     }

//     fn install_vcpkg_dependencies(&self, manifest: &Manifest) -> Result<()> {
//         generate vcpkg.json
//         self.sync_vcpkg_manifest(manifest)?;

//         let vcpkg_manifest_path = self.project_root.join("vcpkg.json");
//         if !vcpkg_manifest_path.exists() {
//             return Ok(());
//         }

//         println!("    Installing dependencies via vcpkg...");

//         let vcpkg_exe = self.vcpkg_exe()?;
//         let triplet = self
//             .triplet
//             .as_ref()
//             .ok_or_else(|| anyhow::anyhow!("Triplet not set for vcpkg"))?;

//         let status = Command::new(&vcpkg_exe)
//             .args(&[
//                 "install",
//                 "--x-manifest-root",
//                 self.project_root
//                     .to_str()
//                     .ok_or_else(|| anyhow::anyhow!("Invalid project root path"))?,
//                 "--x-install-root",
//                 &self.get_install_root().to_string_lossy(),
//                 "--triplet",
//                 triplet,
//             ])
//             .status()
//             .context("Failed to run vcpkg install")?;

//         if !status.success() {
//             bail!("Failed to install dependencies via vcpkg");
//         }

//         Ok(())
//     }

//         fn sync_vcpkg_manifest(&self, manifest: &Manifest) -> Result<()> {
//             let vcpkg_manifest = self.convert_to_vcpkg_manifest(manifest)?;

//             let json = serde_json::to_string_pretty(&vcpkg_manifest)
//                 .context("Failed to serialize vcpkg manifest")?;

//             let vcpkg_manifest_path = self.project_root.join("vcpkg.json");
//             fs::write(&vcpkg_manifest_path, json).context("Failed to write vcpkg.json")?;

//             Ok(())
//         }

//         fn convert_to_vcpkg_manifest(&self, manifest: &Manifest) -> Result<VcpkgManifest> {
//             let mut dependencies = Vec::new();

//             // convert dependencies
//             for (name, version) in &manifest.dependencies {
//                 if version == "*" {
//                     dependencies.push(VcpkgDependency::Simple(name.clone()));
//                 } else {
//                     dependencies.push(VcpkgDependency::Detailed {
//                         name: name.clone(),
//                         version: Some(version.clone()),
//                         features: None,
//                     });
//                 }
//             }

//             // Get baseline
//             let baseline = self.get_vcpkg_baseline().unwrap_or_else(|_| {
//                 eprintln!("Warning: Failed to get vcpkg baseline, using default");
//                 "2024.01.12".to_string()
//             });

//             Ok(VcpkgManifest {
//                 schema:
//                     "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json"
//                         .to_string(),
//                 name: manifest.package.name.clone(),
//                 version: manifest.package.version.clone(),
//                 dependencies,
//                 builtin_baseline: baseline,
//             })
//         }

//         fn get_vcpkg_baseline(&self) -> Result<String> {
//             let vcpkg_root = self
//                 .vcpkg_root
//                 .as_ref()
//                 .ok_or_else(|| anyhow::anyhow!("vcpkg_root not set"))?;

//             let output = Command::new("git")
//                 .args(&["rev-parse", "HEAD"])
//                 .current_dir(vcpkg_root)
//                 .output()
//                 .context("Failed to execute git command")?;

//             if !output.status.success() {
//                 bail!("Failed to get vcpkg git baseline");
//             }

//             let baseline = String::from_utf8_lossy(&output.stdout).trim().to_string();

//             if baseline.is_empty() {
//                 bail!("Empty baseline returned from git");
//             }

//             Ok(baseline)
//         }

//         pub fn get_include_path(&self) -> Vec<PathBuf> {
//             match self.manager_type {
//                 PackageManagerType::Vcpkg => {
//                     if let Some(vcpkg_root) = &self.vcpkg_root {
//                         vec![vcpkg_root.join("installed").join("include")]
//                     } else {
//                         vec![]
//                     }
//                 }
//                 PackageManagerType::Ppargo => vec![],
//             }
//         }

//         pub fn get_lib_paths(&self) -> Vec<PathBuf> {
//             match self.manager_type {
//                 PackageManagerType::Vcpkg => {
//                     if let (Some(vcpkg_root), Some(triplet)) = (&self.vcpkg_root, &self.triplet) {
//                         vec![vcpkg_root.join("installed").join(triplet).join("lib")]
//                     } else {
//                         vec![]
//                     }
//                 }
//                 PackageManagerType::Ppargo => vec![],
//             }
//         }

//     pub fn search_package(&self, package: &str) -> Result<Vec<PackageInfo>> {
//         match self.manager_type {
//             PackageManagerType::Vcpkg => {
//                 let vcpkg_exe = self.vcpkg_exe()?;
//                 let output = Command::new(&vcpkg_exe)
//                     .args(&["search", package])
//                     .output()
//                     .context("Failed to search packages")?;

//                 if !output.status.success() {
//                     bail!("Failed to search for package '{}'", package);
//                 }

//                 let output_str = String::from_utf8_lossy(&output.stdout);
//                 Ok(self.parse_package_list(&output_str))
//             }
//             PackageManagerType::Ppargo => {
//                 Ppargo package search not implemented
//                 Ok(vec![])
//             }
//         }
//     }

//     fn parse_package_list(&self, output: &str) -> Vec<PackageInfo> {
//         let mut packages = Vec::new();

//         for line in output.lines() {
//             if line.trim().is_empty() {
//                 continue;
//             }

//             Parse format: "package:triplet  version  description"
//             let parts: Vec<&str> = line.splitn(3, ' ').collect();
//             if parts.len() >= 2 {
//                 let name_triplet = parts[0];
//                 let version = parts[1].trim();
//                 let description = parts.get(2).map(|s| s.trim().to_string());

//                 let name = name_triplet
//                     .split(':')
//                     .next()
//                     .unwrap_or(name_triplet)
//                     .to_string();

//                 packages.push(PackageInfo {
//                     name,
//                     version: version.to_string(),
//                     description,
//                 });
//             }
//         }

//         packages
//     }

//     pub fn get_install_root(&self) -> PathBuf {
//         self.project.root.join("vcpkg_installed")
//     }
// }

// #[derive(Debug)]
// pub struct PackageInfo {
//     pub name: String,
//     pub version: String,
//     pub description: Option<String>,
// }
