use std::{collections::HashMap, fs, path::{self, Path, PathBuf}};

use anyhow::Result;
use serde::{Deserialize, Serialize};
use crate::core::manifest::{self, Manifest};

#[derive(Serialize, Deserialize)]
pub struct Fingerprint {
    pub files : HashMap<PathBuf, FileInfo>,
    pub compiler_version: String,
    //pub profile: String,
}

#[derive(Serialize, Deserialize)]
pub struct FileInfo {
    pub modified: u64,
    pub size: u64,

}

impl Fingerprint {
    pub fn new(compiler_version: String) -> Self {
        Self {
            files: HashMap::new(),
            compiler_version,
        }
    }

    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {  
        let content = fs::read_to_string(path)?;

        let fingerprint: Fingerprint = serde_json::from_str(&content)?;
        Ok(fingerprint)
    }

    pub fn save<P: AsRef<Path>>(&self, path: P) -> Result<()> {  
       let json = serde_json::to_string_pretty(self)?;
        fs::write(path.as_ref(), json)?;
        Ok(())
    }
}

pub fn create_fingerprint(manifest: &Manifest) -> Result<()> {



    Ok(())
}







// pub fn file_mtime_to fingerprint(mtime: std::time::SystemTime) -> u64 {
//     use std::time::{UNIX_EPOCH, Duration};

//     match mtime.duration_since(UNIX_EPOCH) {
//         Ok(duration) => duration.as_secs(),
//         Err(_) => 0,
//     }
// }