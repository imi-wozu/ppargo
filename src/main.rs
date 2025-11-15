use anyhow::Result;
use clap::Parser;

mod build;
mod cli;
mod core;
mod package;
// mod util;

use cli::{Cli, Commands, commands};

use crate::{build::manager::BuildManager, core::Project};

fn main() -> Result<()> {
    let cli = Cli::parse();

    let mut p: Project = Project::new()?;

    match cli.command {
        Commands::New { name } => {
            commands::new::execute(&name)?;
        }
        Commands::Init => {
            commands::init::execute()?;
        }
        Commands::Add { package } => {
            commands::add::execute(&p, &package)?;
        }
        Commands::Build { release } => {
            commands::build::execute(&p, &BuildManager::new(&p)?, release)?;
        }
        Commands::Run => {
            commands::run::execute(&p, false)?;
        }
        Commands::Version => {
            commands::version::execute()?;
        }
    }

    Ok(())
}
