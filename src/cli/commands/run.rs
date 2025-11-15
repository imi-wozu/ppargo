use anyhow::Result;
use colored::*;

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

    println!(
        "     {} `target/debug/{}`",
        "Running".green().bold(),
        p.manifest.package.name
    );

    bm.run(release)?;

    Ok(())
}
