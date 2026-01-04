use std::sync::OnceLock;

use crate::core::manifest::PackageManagerType;

pub mod manager;
pub mod vcpkg;

static PMTYPE: OnceLock<PackageManagerType> = OnceLock::new();

pub fn get_package_manager_type() -> PackageManagerType {
    *PMTYPE.get_or_init(|| {
        crate::core::get_manifest().features.package_manager
    })
}