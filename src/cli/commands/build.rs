use anyhow::Result;

use std::{
    any, env, fs,
    path::{Path, PathBuf},
};

use crate::{build::manager::BuildManager, core::manifest::find_manifest};

pub fn execute(release: bool) -> Result<()> {
    let manifest_path = find_manifest()?;
    let project_root = manifest_path.parent()
        .ok_or_else(|| anyhow::anyhow!("Invalid manifest path"))?;

    let manager = BuildManager::new(project_root)?;
    manager.build(release)?;

    println!(
        "    Finished {} [optimized] target(s)",
        if release { "release" } else { "debug" }
    );

    Ok(())
}
