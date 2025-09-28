use std::{
    collections::HashMap,
    env, fs,
    path::{Path, PathBuf},
};

use crate::core::{
    manifest::{Manifest, Package},
    templates,
};

pub fn execute() {
    let current_dir = env::current_dir().expect("Failed to get current directory");

    let name = current_dir
        .file_name()
        .and_then(|n| n.to_str())
        .map(String::from)
        .unwrap_or_else(|| "my_project".to_string());

    create_project_structure(&current_dir, &name);

    println!("Initialized C++ project `{}`", name);
}

fn create_project_structure(dir: &PathBuf, name: &str) {
    // create directories
    fs::create_dir_all(dir.join("src")).expect("Failed to create project directory");

    // ceate ppargo.toml
    let manifest: Manifest = templates::crate_manifest(name);
    let toml_str = toml::to_string(&manifest).expect("Failed to serialize manifest");
    fs::write(dir.join("ppargo.toml"), toml_str).expect("Failed to write ppargo.toml");

    // create source files
    let main_cpp = templates::get_main_template();
    fs::write(dir.join("src").join("main.cpp"), main_cpp).expect("Failed to write main.cpp");

    // create .gitignore
    let gitignore = templates::get_gitignore_template();
    fs::write(dir.join(".gitignore"), gitignore).expect("Failed to write .gitignore");
}
