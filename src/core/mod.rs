pub mod manifest;
//pub mod project;
pub mod templates;

use std::{path::PathBuf, sync::OnceLock};

use manifest::Manifest;

static MANIFEST: OnceLock<Manifest> = OnceLock::new();
static ROOT: OnceLock<std::path::PathBuf> = OnceLock::new();

use anyhow::{Context, Result, bail};

pub fn init() -> Result<()> {
    MANIFEST
        .set(manifest::Manifest::load(manifest::find_manifest()?)?)
        .unwrap();
    ROOT.set(std::env::current_dir()?).unwrap();
    Ok(())
}
pub fn get_manifest() -> &'static Manifest {
    MANIFEST.get().expect("Core not initialized")
}

pub fn get_root() -> &'static std::path::PathBuf {
    ROOT.get().expect("Core not initialized")
}

pub fn run(release: bool) -> anyhow::Result<()> {
    let profile = if release { "release" } else { "debug" };
    let build_dir = get_build_dir(profile);
    let binary_name = get_binary_name();
    let executable = build_dir.join(&binary_name);

    // need chainge
    if !executable.exists() {
        bail!("Executable not found. Please build first.");
    }

    let status = std::process::Command::new(&executable)
        .status()
        .context("Failed to run executable")?;

    if !status.success() {
        bail!("Process exited with error");
    }

    Ok(())
}

pub fn get_binary_name() -> String {
    let name = &get_manifest().package.name;
    if cfg!(windows) {
        format!("{}.exe", name)
    } else {
        name.clone()
    }
}

pub fn get_build_dir(profile: &str) -> PathBuf {
    get_root().join("target").join(profile)
}
