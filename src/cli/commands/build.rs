use anyhow::Result;

use std::{
    any, env, fs,
    path::{Path, PathBuf},
};

use crate::{build::manager::BuildManager, core::Project};

pub fn execute(p: &Project, bm: &BuildManager, release: bool) -> Result<()> {
    let start = std::time::Instant::now();

    bm.build(release)?;

    let duration = start.elapsed();

    println!(
        "    Finished {} [optimized] target(s) in {}.{:02} s ",
        if release { "release" } else { "debug" },
        duration.as_secs(),
        duration.subsec_millis() / 10
    );

    Ok(())
}
