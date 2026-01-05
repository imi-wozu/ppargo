use anyhow::{Context, Ok, Result, bail};

use std::{path::PathBuf, process::Command};

use super::PackageInfo;

#[derive(Debug)]
pub struct VcpkgManifest {
    pub exe_root: PathBuf,
}

fn vcpkg_exe() -> Result<PathBuf> {
    let root = crate::core::get_vcpkg_manifest().exe_root.clone();

    let exe = if cfg!(windows) {
        root.join("vcpkg.exe")
    } else {
        root.join("vcpkg")
    };

    if !exe.exists() {
        bail!("vcpkg executable not found at {}", exe.display());
    }

    Ok(exe)
}

pub(super) fn search_package(package: &str) -> anyhow::Result<Vec<super::PackageInfo>> {
    let vcpkg_exe = vcpkg_exe()?;

    let output = Command::new(&vcpkg_exe)
        .args(&["search", package, "--x-full-desc"])
        .output()
        .context("Failed to search packages")?;

    if !output.status.success() {
        bail!("Failed to search for package '{}'", package);
    }

    let output_str = String::from_utf8_lossy(&output.stdout);
    Ok(parse_package_list(&output_str))
}

pub(super) fn install_dependencies(manifest: &crate::core::manifest::Manifest) -> Result<()> {
    let vcpkg_manifest_path = crate::core::get_root().join("vcpkg.json");
    if !vcpkg_manifest_path.exists() {
        let default_vcpkg_json = serde_json::json!({
            "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
            "dependencies": []
        });

        if let Some(parent) = vcpkg_manifest_path.parent() {
            if !parent.exists() {
                std::fs::create_dir_all(parent)?;
            }
        }

        let file_content = serde_json::to_string_pretty(&default_vcpkg_json)?;
        std::fs::write(&vcpkg_manifest_path, file_content)?;
    }

    println!("       Installing dependencies via vcpkg...");

    let vcpkg_exe = vcpkg_exe()?;
    let triplet = Some(crate::util::detect_triplet())
        .ok_or_else(|| anyhow::anyhow!("Triplet not set for vcpkg"))?;

    let status = Command::new(&vcpkg_exe)
        .args(&[
            "install",
            "--x-manifest-root",
            crate::core::get_root()
                .to_str()
                .ok_or_else(|| anyhow::anyhow!("Invalid project root path"))?,
            "--x-install-root",
            &crate::core::get_root()
                .join("packages")
                .to_string_lossy(),
            "--triplet",
            &triplet,
        ])
        .status()
        .context("Failed to run vcpkg install")?;

            if !status.success() {
                bail!("Failed to install dependencies via vcpkg");
            }

    Ok(())
}


fn parse_package_list(output: &str) -> Vec<PackageInfo> {
    let mut packages = Vec::new();

    for line in output.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        let parts: Vec<&str> = line.split_whitespace().collect();

        if parts.len() >= 2 {
            let name = parts[0].to_string();

            if parts[1].chars().next().map_or(false, |c| c.is_digit(10)) {
                let version = parts[1].to_string();
                let description = if parts.len() > 2 {
                    Some(parts[2..].join(" "))
                } else {
                    None
                };

                packages.push(PackageInfo {
                    name,
                    version,
                    description,
                });
            }
        }
    }

    packages
}

pub fn update_vcpkg_manifest(manifest: &mut crate::core::manifest::Manifest) -> Result<()> {
    let vcpkg_manifest_path = crate::core::get_root().join("vcpkg.json");
    let mut vcpkg_json: serde_json::Value = if vcpkg_manifest_path.exists() {
        let content = std::fs::read_to_string(&vcpkg_manifest_path)
            .context("Failed to read vcpkg.json")?;
        serde_json::from_str(&content).context("Failed to parse vcpkg.json")?
    } else {
        serde_json::json!({
            "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg/master/scripts/vcpkg.schema.json",
            "dependencies": []
        })
    };

    let dependencies = vcpkg_json
        .get_mut("dependencies")
        .and_then(|d| d.as_array_mut())
        .ok_or_else(|| anyhow::anyhow!("Invalid vcpkg.json format: 'dependencies' is not an array"))?;

    for (name, version) in &manifest.dependencies {
        if !dependencies.iter().any(|d| d.as_str() == Some(&name)) {
            dependencies.push(serde_json::Value::String(name.clone()));
        }
    }

    let updated_content = serde_json::to_string_pretty(&vcpkg_json)
        .context("Failed to serialize updated vcpkg.json")?;
    std::fs::write(&vcpkg_manifest_path, updated_content)
        .context("Failed to write updated vcpkg.json")?;

    Ok(())
}