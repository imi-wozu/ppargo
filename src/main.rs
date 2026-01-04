use anyhow::Result;
use clap::Parser;

mod build;
mod cli;
mod core;
mod package;
mod util;

use cli::{Cli, Commands, commands};

fn main() {
    if let Err(e) = run() {
        util::print_error(e);
        std::process::exit(1);
    }
}

// Todo: delete all manager

fn run() -> Result<()> {
    core::init()?;

    let cli = Cli::parse();

    match cli.command {
        Commands::New { name } => {
            commands::new::execute(&name)?;
        }
        Commands::Init => {
            commands::init::execute()?;
        }
        Commands::Add { package } => {
            commands::add::execute(&package)?;
        }
        Commands::Build { release } => {
            commands::build::execute(release)?;
        }
        Commands::Run => {
            commands::run::execute(false)?;
        }
        Commands::Version => {
            commands::version::execute()?;
        }
    }

    Ok(())
}
