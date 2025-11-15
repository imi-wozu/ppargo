pub mod manifest;
pub mod templates;


pub struct Project {
    pub root: std::path::PathBuf,
    pub manifest: manifest::Manifest,
}

impl Project {
    pub fn new() -> anyhow::Result<Self> {
        let manifest_path = manifest::find_manifest()?;
        let manifest = manifest::Manifest::load(&manifest_path)?;

        Ok(Self {
            root: std::env::current_dir()?,
            manifest,
        })
    }
}