use anyhow::Error;
use colored::*;

pub fn print_error(e: Error) {
    eprintln!("{} {}", "error:".red().bold(), e);
}

