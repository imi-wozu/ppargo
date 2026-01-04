use crate::{core::get_root, package::manager::{self, PackageManager}};
use anyhow::Result;
use colored::Colorize;
use std::fs;

pub fn execute(package: &str) -> Result<()> {
    println!("      {} {} to dependencies","Adding".green().bold(), package);

    manager::add(package)?;

    println!("       {} {} to dependencies", "Added".green().bold(), package);

    Ok(())
}

// Updating crates.io index
//   Adding colored v3.0.0 to dependencies
//          Features:
//          - no-color
// Updating crates.io index
// Blocking waiting for file lock on package cache
//  Locking 1 package to latest Rust 1.89.0 compatible version
//   Adding colored v3.0.0
