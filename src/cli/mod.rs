pub mod commands;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "argo")]
#[command(about = "Cargo-like C++ build system and package manager (prototype)", long_about = None)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
    /// Create a new ppargo package
    New {
        name: String,
    },

    /// Create a new ppargo package in an existing directory
    Init,

    /// Add dependencies to a manifest file
    Add {
        package: String,
    },

    /// Complie the current package
    #[clap(alias = "b")]
    Build {
        #[arg(long, short = 'r', help = "Build in release mode")]
        release: bool,
    },

    /// Run the package
    #[clap(alias = "r")]
    Run,

    Version
}