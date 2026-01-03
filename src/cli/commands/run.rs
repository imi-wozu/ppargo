use anyhow::Result;
use colored::*;

use crate::core::Project;

pub fn execute(p: &Project, release: bool) -> Result<()> {
    super::build::execute(p, release)?;

    println!(
        "     {} `target/debug/{}`",
        "Running".green().bold(),
        p.manifest.package.name
    );

    p.run(release)?;

    Ok(())
}
