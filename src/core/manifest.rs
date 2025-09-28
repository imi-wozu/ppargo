use serde::{Deserialize, Serialize};
use std::{collections::HashMap, result::Result, path::Path, fs};

#[derive(Debug, Serialize, Deserialize)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub dependencies: HashMap<String, String>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Package {
    pub name: String,
    pub version: String,
}

// cwd = current working directory

pub fn load_manifest_from_cwd() -> Result<Manifest, String> {
    let manifest_path = Path::new("Cppargo.toml");
    if !manifest_path.exists() {
        return Err(
            "Cppargo.toml not found in current directory. Run `argo init <name>` first.".into(),
        );
    }
    let content = fs::read_to_string(manifest_path)
        .map_err(|e| format!("Failed to read Cppargo.toml: {}", e))?;
    toml::from_str(&content).map_err(|e| format!("Invalid Cppargo.toml format: {}", e))
}

// /// Cppargo.toml 저장 (덮어쓰기)
// fn save_manifest_to_cwd(manifest: &Manifest) -> Result<(), String> {
//     let toml_str = toml::to_string(&manifest).map_err(|e| format!("Failed to serialize manifest: {}", e))?;
//     fs::write("Cppargo.toml", toml_str).map_err(|e| format!("Failed to write Cppargo.toml: {}", e))
// }