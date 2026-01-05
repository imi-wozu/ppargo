use anyhow::Result;
use colored::*;

pub fn execute( release: bool) -> Result<()> {
    println!(
        "   {} {} v{} ({})",
        "Compiling".green().bold(),
        crate::core::get_manifest().package.name,
        crate::core::get_manifest().package.version,
        crate::core::get_root().display(),
    );

    let start = std::time::Instant::now();

    crate::build::build(release)?;

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
