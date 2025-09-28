use serde::{Deserialize, Serialize};
use std::{collections::HashMap, result::Result, path::Path, fs};

#[derive(Debug, Serialize, Deserialize)]
pub struct Manifest {
    pub package: Package,

    #[serde(default)]
    pub dependencies: HashMap<String, String>,

    // #[serde(default, rename = "dev-dependencies")]
    // pub dev_dependencies: HashMap<String, Dependency>,
    
    // #[serde(default)]
    // pub build: BuildConfig,
    
    // #[serde(default)]
    // pub toolchain: Toolchain,
    
    // #[serde(skip_serializing_if = "Option::is_none")]
    // pub workspace: Option<Workspace>,
    
    // #[serde(default)]
    // pub profiles: HashMap<String, Profile>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Package {
    pub name: String,
    pub version: String,

    #[serde(default = "default_edition")]
    pub edition: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct BuildConfig {
//     #[serde(default = "default_source_dir")]
//     pub source_dir: PathBuf,
    
//     #[serde(default = "default_include_dir")]
//     pub include_dir: PathBuf,
    
//     #[serde(default = "default_build_dir")]
//     pub build_dir: PathBuf,
    
//     #[serde(default)]
//     pub defines: Vec<String>,
    
//     #[serde(default)]
//     pub flags: Vec<String>,
    
//     #[serde(default)]
//     pub link_flags: Vec<String>,
    
//     #[serde(default)]
//     pub include_paths: Vec<PathBuf>,
    
//     #[serde(default)]
//     pub lib_paths: Vec<PathBuf>,
    
//     #[serde(default)]
//     pub libs: Vec<String>,
}



// cwd = current working directory

pub fn load_manifest_from_cwd() -> Result<Manifest, String> {
    let manifest_path = Path::new("ppargo.toml");
    if !manifest_path.exists() {
        return Err(
            "ppargo.toml not found in current directory. Run `argo init <name>` first.".into(),
        );
    }
    let content = fs::read_to_string(manifest_path)
        .map_err(|e| format!("Failed to read ppargo.toml: {}", e))?;
    toml::from_str(&content).map_err(|e| format!("Invalid ppargo.toml format: {}", e))
}

// /// Cppargo.toml 저장 (덮어쓰기)
// fn save_manifest_to_cwd(manifest: &Manifest) -> Result<(), String> {
//     let toml_str = toml::to_string(&manifest).map_err(|e| format!("Failed to serialize manifest: {}", e))?;
//     fs::write("Cppargo.toml", toml_str).map_err(|e| format!("Failed to write Cppargo.toml: {}", e))
// }

fn default_edition() -> String {
    "cpp17".to_string()
}