use anyhow::{Context, Ok, Result, bail};
use reqwest::Version;
use serde::{Deserialize, Serialize};
use std::{
    env, fs,
    path::{Path, PathBuf},
    process::Command,
    sync::OnceLock,
};

use super::vcpkg;
use crate::{
    cli::commands::add,
    core::{get_root, manifest::PackageManagerType},
    package::get_package_manager_type,
};

static VCPKG_MANIFEST: OnceLock<vcpkg::VcpkgManifest> = OnceLock::new();

pub(super) fn get_vcpkg_manifest() -> &'static vcpkg::VcpkgManifest {
    VCPKG_MANIFEST
        .get()
        .expect("Vcpkg manifest not initialized")
}

pub struct PackageManager {
    pub manager_type: PackageManagerType,
}

pub struct PackageInfo {
    pub name: String,
    pub version: String,
    pub description: Option<String>,
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

pub fn add(package: &str) -> Result<()> {
    // Check if packages feature is enabled
    if !crate::core::get_manifest().features.packages {
        anyhow::bail!(
            "Package management is disabled. Enable it in ppargo.toml:\n[features]\npackages = true"
        )
    }

    // Initialize package manager
    let package_manager = PackageManager::new()?;

    // Verify package exists
    let search_result = package_manager.search_package(package)?;

    // Print search results for visibility
    if search_result.is_empty() {
        anyhow::bail!(
            "Package '{}' not found in the {} registry",
            package,
            match crate::core::get_manifest().features.package_manager {
                crate::core::manifest::PackageManagerType::Ppargo => "ppargo",
                crate::core::manifest::PackageManagerType::Vcpkg => "vcpkg",
            }
        );
    } else {
        // println!("       Found {} package(s):", search_result.len());
        // for pkg in &search_result {
        //     let desc = pkg.description.as_deref().unwrap_or("");
        //     if desc.is_empty() {
        //         println!("         - {} {}", pkg.name, pkg.version);
        //     } else {
        //         println!("         - {} {} - {}", pkg.name, pkg.version, desc);
        //     }
        // }
    }

    // Add to manifest
    let mut updated_manifest = crate::core::get_manifest().clone();
    updated_manifest.add_dependency(package, &search_result[0].version);

    // Add dependency to manifest
    match get_package_manager_type() {
        PackageManagerType::Vcpkg => {
            // Update vcpkg manifest
            vcpkg::update_vcpkg_manifest(&mut updated_manifest)?;
        }
        PackageManagerType::Ppargo => {
            //ppargo dependency addition not implemented
        }
    }

    // Save manifest
    let toml_str: String = toml::to_string(&updated_manifest)?;
    fs::write(&get_root().join("ppargo.toml"), toml_str)?;

    // Install dependencies
    // println!("       Installing {}...", package);
    package_manager.install_dependencies(&updated_manifest)?;

    Ok(())
}

impl PackageManager {
    pub fn new() -> Result<Self> {
        match crate::core::get_manifest().features.package_manager {
            PackageManagerType::Vcpkg => {
                let exe_root = crate::core::get_manifest()
                    .features
                    .vcpkg_root
                    .clone()
                    .ok_or_else(|| {
                        anyhow::anyhow!("vcpkg_root must be specified in features when using vcpkg")
                    })?;

                let vcpkg = vcpkg::VcpkgManifest { exe_root };

                VCPKG_MANIFEST.set(vcpkg).unwrap();
                Ok(Self {
                    manager_type: PackageManagerType::Vcpkg,
                })
            }
            PackageManagerType::Ppargo => Ok(Self {
                manager_type: PackageManagerType::Ppargo,
            }),
        }
    }

    pub fn install_dependencies(&self, manifest: &crate::core::manifest::Manifest) -> Result<()> {
        match manifest.features.package_manager {
            PackageManagerType::Vcpkg => super::vcpkg::install_dependencies(manifest),
            PackageManagerType::Ppargo => {
                Ok(())
                //ppargo dependency installation
            }
        }
    }

    pub fn search_package(&self, package: &str) -> Result<Vec<PackageInfo>> {
        match self.manager_type {
            PackageManagerType::Vcpkg => super::vcpkg::search_package(package),
            PackageManagerType::Ppargo => {
                //Ppargo package search not implemented
                Ok(vec![])
            }
        }
    }
}
