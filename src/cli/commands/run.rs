use anyhow::Result;
use std::{path::{Path, PathBuf}, fs, process::Command};

use crate::{build::{self, manager::BuildManager}, core::manifest::{self, find_manifest, load_manifest_from_cwd}};


pub fn execute(release: bool) -> Result<()> {
    // First build the project
    super::build::execute(release)?;

    let manifest_path = find_manifest()?;

    let project_root = manifest_path.parent().ok_or_else(|| anyhow::anyhow!("Invalid manifest path"))?;

    let manager = BuildManager::new(project_root)?;
    manager.run(release)?;
    Ok(())
}
