pub mod manifest;
pub mod templates;

use anyhow::{Context, Result, bail};
use std::{path::PathBuf, sync::OnceLock};

use manifest::{Manifest, PackageManagerType};

static MANIFEST: OnceLock<Manifest> = OnceLock::new();
static ROOT: OnceLock<std::path::PathBuf> = OnceLock::new();
static PMTYPE: OnceLock<PackageManagerType> = OnceLock::new();
static VCPKG_MANIFEST: OnceLock<crate::package::vcpkg::VcpkgManifest> = OnceLock::new();

pub fn init() -> Result<()> {
    MANIFEST
        .set(manifest::Manifest::load(manifest::find_manifest()?)?)
        .unwrap();
    ROOT.set(std::env::current_dir()?).unwrap();

    match get_package_manager_type() {
        PackageManagerType::Vcpkg => {
            let exe_root = get_manifest().features.vcpkg_root.clone().ok_or_else(|| {
                anyhow::anyhow!("vcpkg_root must be specified in features when using vcpkg")
            })?;
            let vcpkg = crate::package::vcpkg::VcpkgManifest { exe_root };
            VCPKG_MANIFEST.set(vcpkg).unwrap();
        }
        PackageManagerType::Ppargo => {
            // No additional initialization needed for Ppargo
        }
    };

    Ok(())
}
pub fn get_manifest() -> &'static Manifest {
    MANIFEST.get().expect("Core not initialized")
}

pub fn get_root() -> &'static std::path::PathBuf {
    ROOT.get().expect("Core not initialized")
}

pub fn get_package_manager_type() -> PackageManagerType {
    *PMTYPE.get_or_init(|| crate::core::get_manifest().features.package_manager)
}

pub fn get_vcpkg_manifest() -> &'static crate::package::vcpkg::VcpkgManifest {
    VCPKG_MANIFEST
        .get()
        .expect("Vcpkg manifest not initialized")
}

pub fn run(release: bool) -> anyhow::Result<()> {
    let build_dir = get_build_dir(release);
    let binary_name = get_binary_name();
    let executable = build_dir.join(&binary_name);

    // need chainge
    if !executable.exists() {
        bail!("Executable not found. Please build first.");
    }

    let status = std::process::Command::new(&executable)
        .status()
        .context("Failed to run executable")?;

    if !status.success() {
        bail!("Process exited with error");
    }

    Ok(())
}

pub fn get_binary_name() -> String {
    let name = &get_manifest().package.name;
    if cfg!(windows) {
        format!("{}.exe", name)
    } else {
        name.clone()
    }
}

pub fn get_build_dir(release: bool) -> PathBuf {
    get_root().join("target").join(if release { "release" } else { "debug" })
}

