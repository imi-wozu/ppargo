use anyhow::Result;
use clap::{Parser, Subcommand};

mod build;
mod cli;
mod core;
mod package;
// mod util;

use cli::commands;

#[derive(Parser)]
#[command(name = "argo")]
#[command(about = "Cargo-like C++ build system and package manager (prototype)", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
    // /// Path to Cppargo.toml
    // #[arg(long, global = true)]
    // manifest_path: Option<PathBuf>,

    // /// Verbose output
    // #[arg(short, long, global = true)]
    // verbose: bool,
}

#[derive(Subcommand)]
enum Commands {
    // Create a new cppargo package
    New {
        name: String,
    },

    /// Create a new cppargo package in an existing directory
    Init,

    /// 의존성 설치 (vcpkg backend) + manifest 자동 업데이트
    // add {
    //     package: String,
    // },

    /// Complie the current package
    #[clap(alias = "b")]
    Build {
        #[arg(long, short = 'r', help = "Build in release mode")]
        release: bool,
    },

    /// Run the package
    #[clap(alias = "r")]
    Run,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    // Initialize logger
    // util::logger::init_logger(cli.verbose)?;

    match cli.command {
        Commands::New { name } => {
            commands::new::execute(&name);
        }
        Commands::Init => {
            commands::init::execute();
        }
        Commands::Build { release } => {
            commands::build::execute(release);
        }
        Commands::Run => {
            commands::run::execute(false);
        }
    }

    Ok(())
}
