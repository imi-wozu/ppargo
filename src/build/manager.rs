use anyhow::{Context, Ok, Result, bail};

use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use walkdir::WalkDir;

use crate::{
    core::Project,
    package::manager::PackageManager,
};

pub struct BuildManager<'a> {
    pub project: &'a Project,

    compiler: PathBuf,
    // linker: Option<PathBuf>,
    package_manager: Option<PackageManager<'a>>,
}

impl<'a> BuildManager<'a> {
    pub fn new(project: &'a Project) -> Result<Self> {
        let compiler = Self::find_compiler(&project.manifest.toolchain.compiler)?;

        let package_manager = if project.manifest.features.packages {
            Some(PackageManager::new(project)?)
        } else {
            None
        };

        Ok(Self {
            project,
            compiler,
            package_manager,
        })
    }

    fn find_compiler(compiler: &str) -> Result<PathBuf> {
        which::which(compiler).context(format!("Compiler '{}' not found in PATH", compiler))
    }

    pub fn build(&self, release: bool) -> Result<()> {
        // Ensure dependencies are installed (if enabled)
        if let Some(pm) = &self.package_manager {
            //  pm.install_dependencies(&self.project.manifest)?;
        }

        let profile = if release { "release" } else { "debug" };
        let build_dir = self.get_build_dir(profile);

        // Create build directory
        fs::create_dir_all(&build_dir).context("Failed to create build directory")?;

        // Collect source files
        let source_files = self.collect_source_files()?;

        // Compile sources
        let object_files = self.compile_sources(&source_files, &build_dir, release)?;

        // Link
        self.link_executable(&object_files, &build_dir, release)?;

        Ok(())
    }

    fn get_build_dir(&self, profile: &str) -> PathBuf {
        self.project.root.join("target").join(profile)
    }

    fn collect_source_files(&self) -> Result<Vec<PathBuf>> {
        let mut sources = Vec::new();
        let src_dir = self.project.root.join("src");

        if !src_dir.exists() {
            bail!("Source directory 'src/' does not exist");
        }

        for entry in WalkDir::new(&src_dir) {
            let entry = entry?;
            let path = entry.path();

            if path.is_file() {
                if let Some(ext) = path.extension() {
                    if matches!(
                        ext.to_str(),
                        Some("cpp") | Some("cxx") | Some("cc") | Some("c")
                    ) {
                        sources.push(path.to_path_buf());
                    }
                }
            }
        }

        Ok(sources)
    }

    fn compile_sources(
        &self,
        sources: &[PathBuf],
        build_dir: &Path,
        release: bool,
    ) -> Result<Vec<PathBuf>> {
        let obj_dir = build_dir.join("obj");
        fs::create_dir_all(&obj_dir);

        // Create obj directory
        let mut object_files = Vec::new();

        for source in sources {
            let obj_file = self.get_object_file_path(source, &obj_dir)?;

            if self.needs_rebuild(source, &obj_file)? {
                self.compile_single_file(source, &obj_file, release)?;
            }

            object_files.push(obj_file);
        }

        Ok(object_files)
    }

    fn get_object_file_path(&self, source: &Path, obj_dir: &Path) -> Result<PathBuf> {
        let relatives = source.strip_prefix(&self.project.root).unwrap_or(source);
        let mut obj_path = obj_dir.join(relatives);
        obj_path.set_extension("o");

        if let Some(parent) = obj_path.parent() {
            fs::create_dir_all(parent)?;
        }

        Ok(obj_path)
    }

    fn needs_rebuild(&self, source: &Path, object: &Path) -> Result<bool> {
        if !object.exists() {
            return Ok(true);
        }

        let source_modified = fs::metadata(source)?.modified()?;
        let object_modified = fs::metadata(object)?.modified()?;

        Ok(source_modified > object_modified)
    }

    fn compile_single_file(&self, source: &Path, output: &Path, release: bool) -> Result<()> {
        let mut cmd = Command::new(&self.compiler);

        // Basic flags
        cmd.arg("-c").arg(source).arg("-o").arg(output);

        // C++ standard
        let std_flag = match self.project.manifest.package.edition.as_str() {
            "cpp20" => "-std=c++20",
            "cpp23" => "-std=c++23",
            _ => "-std=c++17",
        };
        cmd.arg(std_flag);

        // Optimization
        if release {
            cmd.arg("-03");
        } else {
            cmd.arg("-O0").arg("-g");
        }

        // Warnings
        cmd.arg("-Wall").arg("-Wextra");

        // Include paths
        let include_dir = self.project.root.join("include");
        if include_dir.exists() {
            cmd.arg(format!("-I{}", include_dir.display()));
        }

        // Package include paths (if enabled)
        if let Some(pm) = &self.package_manager {
            // for path in pm.get_include_path() {
            //     cmd.arg(format!("-I{}", path.display()));
            // }
        }

        let output_result = cmd.output().context("Failed to execute compiler")?;

        if !output_result.status.success() {
            eprintln!("{}", String::from_utf8_lossy(&output_result.stderr));
            bail!("Compilation failed for {:?}", source);
        }

        Ok(())
    }

    fn link_executable(&self, objects: &[PathBuf], build_dir: &Path, release: bool) -> Result<()> {
        let binary_name = self.get_binary_name();
        let output = build_dir.join(&binary_name);

        let mut cmd = Command::new(&self.compiler);

        // Object files
        for obj in objects {
            cmd.arg(obj);
        }

        // Output
        cmd.arg("-o").arg(&output);

        // Optimization
        if release {
            cmd.arg("-O3");
            cmd.arg("-s"); // Strip symbols
        }

        // Library paths (if enabled)
        if let Some(pm) = &self.package_manager {
            // for path in pm.get_lib_paths() {
            //     cmd.arg(format!("-L{}", path.display()));
            // }
        }

        // Execute linking
        let output_result = cmd.output().context("Failed to execute linker")?;

        if !output_result.status.success() {
            eprintln!("{}", String::from_utf8_lossy(&output_result.stderr));
            bail!("Linking failed");
        }

        Ok(())
    }

    fn get_binary_name(&self) -> String {
        let name = &self.project.manifest.package.name;
        if cfg!(windows) {
            format!("{}.exe", name)
        } else {
            name.clone()
        }
    }

    pub fn run(&self, release: bool) -> Result<()> {
        let profile = if release { "release" } else { "debug" };
        let build_dir = self.get_build_dir(profile);
        let binary_name = self.get_binary_name();
        let executable = build_dir.join(&binary_name);

        if !executable.exists() {
            bail!("Executable not found. Please build first.");
        }

        let status = Command::new(&executable)
            .status()
            .context("Failed to run executable")?;

        if !status.success() {
            bail!("Process exited with error");
        }

        Ok(())
    }
}
