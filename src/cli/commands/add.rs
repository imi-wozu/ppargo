use crate::package::manager::PackageManager;
use anyhow::Result;
use std::fs;

pub fn execute(package: &str) -> Result<()> {
    println!("      Adding {} to dependencies", package);

    // Check if packages feature is enabled
    if !crate::core::get_manifest().features.packages {
        anyhow::bail!(
            "Package management is disabled. Enable it in ppargo.toml:\n[features]\npackages = true"
        )
    }

    // Initialize package manager
   // let package_manager = PackageManager::new()?;

    // Verify package exists
    //let search_result = package_manager.search_package(package)?;

    // if search_result.is_empty() {
    //     anyhow::bail!("Package '{}' not found in the {} registry", package, match p.manifest.features.package_manager {
    //         crate::core::manifest::PackageManagerType::Ppargo => "ppargo",
    //         crate::core::manifest::PackageManagerType::Vcpkg => "vcpkg",
    //     });
    // }

    // if let Some(pkg_info) = search_result.first() {
    //     println!("       Found package: {} - {}", pkg_info.name, pkg_info.version);
    // }

    // Add to manifest
    let pkg_version = "*".to_string();
    //p.manifest.add_dependency(package, &pkg_version);

    // Save manifest
    // let toml_str: String = toml::to_string(&p.manifest)?;
    // fs::write(&manifest_path, toml_str)?;

    // Install dependencies
    // println!("       Installing {}...", package);
    // package_manager.install_dependencies(&manifest)?;

    println!("       Added {} to dependencies", package);

    Ok(())
}

// Updating crates.io index
//   Adding colored v3.0.0 to dependencies
//          Features:
//          - no-color
// Updating crates.io index
// Blocking waiting for file lock on package cache
//  Locking 1 package to latest Rust 1.89.0 compatible version
//   Adding colored v3.0.0
