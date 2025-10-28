use anyhow::{Ok, Result};
use std::path::{Path, PathBuf};

use crate::core::manifest::Manifest;

pub struct PackageManager {


}

impl PackageManager {
    pub fn new() -> Self {
        Self {}
    }

    pub fn install_dependencies(&self, manifest: &Manifest) -> Result<()> {
        Ok(())
    }

    pub fn get_include_path(&self) -> Vec<PathBuf>{
        vec![]
    }

    pub fn get_lib_paths(&self) -> Vec<PathBuf> {
        vec![]
    }
}
