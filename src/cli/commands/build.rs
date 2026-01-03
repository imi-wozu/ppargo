use anyhow::Result;
use colored::*;

use crate::{build::manager::BuildManager, };

pub fn execute( release: bool) -> Result<()> {
    let bm = BuildManager::new()?;
    println!(
        "   {} {} v{} ({})",
        "Compiling".green().bold(),
        crate::core::get_manifest().package.name,
        crate::core::get_manifest().package.version,
        crate::core::get_root().display(),
    );

    let start = std::time::Instant::now();

    bm.build(release)?;

    let duration = start.elapsed();

    println!(
        "    {} {} [optimized] target(s) in {}.{:02} s ",
        "Finished".green().bold(),
        if release { "release" } else { "debug" },
        duration.as_secs(),
        duration.subsec_millis() / 10
    );

    Ok(())
}
