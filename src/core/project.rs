use anyhow::{Context, Ok, Result, bail};

use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use super::{Manifest, manifest};

pub struct Project {
    pub root: std::path::PathBuf,
    pub manifest: Manifest,
}

impl Project {
    pub fn new() -> anyhow::Result<Self> {
        let manifest_path = manifest::find_manifest()?;
        let manifest = manifest::Manifest::load(&manifest_path)?;

        Ok(Self {
            root: std::env::current_dir()?,
            manifest,
        })
    }

    pub fn run(&self, release: bool) -> anyhow::Result<()> {
        let profile = if release { "release" } else { "debug" };
        let build_dir = self.get_build_dir(profile);
        let binary_name = self.get_binary_name();
        let executable = build_dir.join(&binary_name);

        // need chainge
        if !executable.exists() {
            bail!("Executable not found. Please build first.");
        }

        let status = Command::new(&executable)
            .status()
            .context("Failed to run executable")?;

        if !status.success() {
            bail!("Process exited with error");
        }

        Ok(())
    }

    pub fn get_binary_name(&self) -> String {
        let name = &self.manifest.package.name;
        if cfg!(windows) {
            format!("{}.exe", name)
        } else {
            name.clone()
        }
    }

    pub fn get_build_dir(&self, profile: &str) -> PathBuf {
        self.root.join("target").join(profile)
    }
}
