use anyhow::Result;
use colored::*;

use crate::{build::manager::BuildManager, core::project::Project};

pub fn execute(p: &Project, release: bool) -> Result<()> {
    let bm = BuildManager::new(p)?;
    println!(
        "   {} {} v{} ({})",
        "Compiling".green().bold(),
        p.manifest.package.name,
        p.manifest.package.version,
        p.root.display(),
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
