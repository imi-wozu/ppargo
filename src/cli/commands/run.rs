use anyhow::Result;
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use crate::{
    build::{self, manager::BuildManager},
    core::{
        Project,
        manifest::{self},
    },
};

pub fn execute(p: &Project, release: bool) -> Result<()> {
    let bm = BuildManager::new(p)?;
    super::build::execute(p, &bm, release)?;

    println!("     Running `target/debug/{}`", p.manifest.package.name);

    bm.run(release)?;

    Ok(())
}
