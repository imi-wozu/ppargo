use anyhow::Result;
use colored::*;

pub fn execute( release: bool) -> Result<()> {
    super::build::execute( release)?;

    println!(
        "     {} `target/debug/{}`",
        "Running".green().bold(),
        crate::core::get_manifest().package.name
    );

    crate::core::run(release)?;

    Ok(())
}
