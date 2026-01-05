use anyhow::Result;

use std::{fs, sync::OnceLock};

use crate::core::{get_manifest, get_package_manager_type, get_root, manifest::PackageManagerType};

pub mod vcpkg;


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

    // Verify package exists
    let search_result = search_package(package)?;

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
    match crate::core::get_package_manager_type() {
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
    install_dependencies(&updated_manifest)?;

    Ok(())
}

pub fn install_dependencies(manifest: &crate::core::manifest::Manifest) -> Result<()> {
    match manifest.features.package_manager {
        PackageManagerType::Vcpkg => vcpkg::install_dependencies(manifest),
        PackageManagerType::Ppargo => {
            Ok(())
            //ppargo dependency installation
        }
    }
}

pub fn search_package(package: &str) -> Result<Vec<PackageInfo>> {
    match get_package_manager_type(){
        PackageManagerType::Vcpkg => vcpkg::search_package(package),
        PackageManagerType::Ppargo => {
            //Ppargo package search not implemented
            Ok(vec![])
        }
    }
}
